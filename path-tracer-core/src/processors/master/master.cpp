#include "master.hpp"
#include "cloud/messaging.hpp"
#include "cloud/s3.hpp"
#include "models/cloud_ray.hpp"
#include "models/messaging.hpp"
#include "path_tracer/core/utils.hpp"
#include "path_tracer/image/image.hpp"

namespace processors
{
master::master(const models::worker_info &worker_info)
{
    this->m_worker_info = worker_info;
}

master::~master()
{
}

void master::run()
{
    this->resolution =
        math::uvec2((m_worker_info.max_x - m_worker_info.min_x) + 1, (m_worker_info.max_y - m_worker_info.min_y) + 1);
    this->sample_count = m_worker_info.samples;
    this->m_should_terminate = false;
    this->m_completed_rays = 0;

    pixels.resize(resolution.x);
    for (auto &column : pixels)
        column.resize(resolution.y, {math::fvec3::zero, 0, false, 0});

    std::vector<std::thread> threads;
    threads.push_back(std::thread(&master::process_accumulation, this));

    threads.push_back(std::thread(([&]() {
        uint32_t total_rays = resolution.x * resolution.y * sample_count;
        while (m_completed_rays < total_rays)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        m_should_terminate = true;

        cloud::sns_signal_termination(m_worker_info.sns_topic_arn, models::WORKERS_ID);

        spdlog::info("All rays processed, signaling termination");
    })));

    models::SQSOptions sqs_options;
    sqs_options.queueUrl = m_worker_info.sqs_queue_url;

    threads.push_back(std::thread([&]() {
        cloud::sqs_poll(sqs_options, m_should_terminate, [&](models::cloud_ray &ray) { process_ray_from_queue(ray); });
    }));

    for (auto &thread : threads)
        thread.join();

    spdlog::info("All threads have completed execution.");
    spdlog::info("Generating Image...");

    auto png_data = generate_final_image();
    std::variant<std::filesystem::path, std::vector<uint8_t>> input{png_data};
    spdlog::info("Uploading image...");
    cloud::s3_upload_object(m_worker_info.scene_bucket, m_worker_info.scene_root + "test.png", input);
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