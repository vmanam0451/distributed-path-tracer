#include "master.hpp"
#include "cloud/s3.hpp"
#include "cloud/tcp_peer.hpp"
#include "models/cloud_ray.hpp"
#include "path_tracer/core/utils.hpp"
#include "path_tracer/image/image.hpp"
#include "path_tracer/math/math.hpp"
#include <chrono>
#include <csignal>

namespace processors
{

// Static member definitions
std::atomic<bool> master::s_signal_received{false};
master *master::s_instance = nullptr;

void master::signal_handler(int signum)
{
    spdlog::warn("Received signal {} ({}), initiating graceful shutdown...", signum,
                 signum == SIGTERM  ? "SIGTERM"
                 : signum == SIGINT ? "SIGINT"
                                    : "OTHER");
    s_signal_received = true;
    if (s_instance)
    {
        s_instance->m_should_terminate = true;
    }
}

void master::setup_signal_handlers()
{
    s_instance = this;
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGINT, signal_handler);
    spdlog::info("Signal handlers installed for SIGTERM and SIGINT");
}

void master::handle_termination_signal()
{
    spdlog::info("Handling termination signal - generating and uploading partial image...");

    uint32_t total_rays = resolution.x * resolution.y * sample_count;
    float progress = 100.0f * m_completed_rays.load() / total_rays;
    spdlog::info("Progress at termination: {:.1f}% ({}/{} rays)", progress, m_completed_rays.load(), total_rays);

    // Signal termination to all workers via TCP
    if (m_tcp_peer)
    {
        m_tcp_peer->send_terminate_all();
    }

    spdlog::info("Generating partial image...");
    auto png_data = generate_final_image();

    std::variant<std::filesystem::path, std::vector<uint8_t>> input{png_data};
    spdlog::info("Uploading partial image...");
    cloud::s3_upload_object(m_worker_info.scene_bucket, m_worker_info.scene_root + "partial_render.png", input);
    spdlog::info("Partial image uploaded successfully");
}

master::master(const models::worker_info &worker_info)
{
    this->m_worker_info = worker_info;

    m_tcp_peer = std::make_shared<cloud::tcp_peer>(worker_info.worker_id, cloud::DEFAULT_TCP_PORT);
}

master::~master()
{
    if (m_tcp_peer)
    {
        m_tcp_peer->stop();
    }
}

void master::run()
{
    this->resolution =
        math::uvec2((m_worker_info.max_x - m_worker_info.min_x), (m_worker_info.max_y - m_worker_info.min_y));
    this->sample_count = m_worker_info.samples;
    this->m_should_terminate = false;
    this->m_completed_rays = 0;
    s_signal_received = false;

    setup_signal_handlers();

    pixels.resize(resolution.x);
    for (auto &column : pixels)
        column.resize(resolution.y, {math::fvec3::zero, 0, false, 0});

    // Master peers = N workers (discovered via Cloud Map) + 1 web backend
    // (registered statically using the host/port baked into worker_info).
    const bool has_web_peer = !m_worker_info.web_host.empty() && m_worker_info.web_port != 0;
    int expected_peers = m_worker_info.num_workers + (has_web_peer ? 1 : 0);
    spdlog::info("Starting TCP peer for master (expecting {} peers: {} workers + {} web)", expected_peers,
                 m_worker_info.num_workers, has_web_peer ? 1 : 0);
    m_tcp_peer->set_ray_callback([this](models::cloud_ray &ray) { process_ray_from_queue(ray); });
    m_tcp_peer->set_terminate_callback([this]() { m_should_terminate = true; });
    m_tcp_peer->start(m_worker_info.cloud_map_namespace, m_worker_info.cloud_map_service,
                      m_worker_info.cloud_map_service_id, expected_peers, m_worker_info.aws_region);

    if (has_web_peer)
    {
        spdlog::info("Registering web backend peer at {}:{}", m_worker_info.web_host, m_worker_info.web_port);
        m_tcp_peer->register_peer(models::WEB_ID, m_worker_info.web_host, m_worker_info.web_port);
    }
    else
    {
        spdlog::warn("No web_host/web_port configured; pixel batches will be discarded");
    }

    if (!m_tcp_peer->wait_for_peers(120))
    {
        spdlog::error("Failed to connect to all peers, aborting");
        return;
    }

    std::vector<std::thread> threads;
    threads.push_back(std::thread(&master::process_accumulation, this));

    threads.push_back(std::thread(([&]() {
        uint32_t total_rays = resolution.x * resolution.y * sample_count;
        while (!s_signal_received)
        {
            float progress = 100.0f * m_completed_rays.load() / total_rays;
            spdlog::info("Progress: {:.1f}% ({}/{} rays)", progress, m_completed_rays.load(), total_rays);

            if ((progress + 2) >= 100.0f)
                break;

            std::this_thread::sleep_for(std::chrono::seconds(5));
        }

        m_should_terminate = true;
        // Give the pixel flush thread a moment to push the last accumulated
        // batch to the web backend before we tear connections down. The flush
        // interval is 100ms, so 500ms is comfortably more than one tick.
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        // TCP TERMINATE is broadcast to every connected peer, including the
        // web backend, which closes its SSE stream when it sees it.
        m_tcp_peer->send_terminate_all();

        if (s_signal_received)
        {
            spdlog::info("Termination signal received, stopping processing");
        }
        else
        {
            spdlog::info("All rays processed, signaling termination");
        }
    })));

    for (auto &thread : threads)
        thread.join();

    spdlog::info("All threads have completed execution.");

    if (s_signal_received)
    {
        handle_termination_signal();
    }
    else
    {
        spdlog::info("Generating final image...");
        auto png_data = generate_final_image();
        std::variant<std::filesystem::path, std::vector<uint8_t>> input{png_data};
        spdlog::info("Uploading image...");
        cloud::s3_upload_object(m_worker_info.scene_bucket, m_worker_info.scene_root + "test.png", input);
    }

    // Clear static instance pointer
    s_instance = nullptr;
}

std::vector<uint8_t> master::generate_final_image()
{
    using namespace math;

    auto img = std::make_shared<image::image>(resolution, 4, false, true);

    for (uint32_t y = 0; y < resolution.y; y++)
    {
        for (uint32_t x = 0; x < resolution.x; x++)
        {
            fvec3 color = core::tonemap_approx_aces(pixels[x][y].color);
            float alpha = pixels[x][y].alpha;

            uvec2 pixel(x, y);
            img->write(pixel, 0, color.x);
            img->write(pixel, 1, color.y);
            img->write(pixel, 2, color.z);
            img->write(pixel, 3, alpha);
        }
    }

    return img->save_to_memory_png();
}
void master::process_ray_from_queue(const models::cloud_ray &ray)
{
    if (ray.stage == models::ray_stage::ACCUMULATE)
    {
        m_accumulate_queue.enqueue(ray);
    }
}
} // namespace processors