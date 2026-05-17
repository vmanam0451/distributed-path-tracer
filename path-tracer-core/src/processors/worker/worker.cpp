#include "worker.hpp"

#include <cstdint>
#include <path_tracer/core/pbr.hpp>
#include <path_tracer/core/renderer.hpp>
#include <path_tracer/core/utils.hpp>
#include <path_tracer/image/image.hpp>
#include <path_tracer/math/vec2.hpp>
#include <path_tracer/math/vec3.hpp>
#include <path_tracer/util/thread_pool.hpp>

#include "cloud/s3.hpp"
#include "cloud/tcp_peer.hpp"
#include "models/cloud_ray.hpp"
#include "path_tracer/util/rand_cone_vec.hpp"

namespace processors
{
worker::worker(const models::worker_info &worker_info)
    : m_worker_info(worker_info), m_gltf_file_path(std::filesystem::path("/tmp/scene.gltf"))
{
    // Initialize TCP peer for direct communication
    spdlog::info("Initializing TCP peer for direct communication");
    m_tcp_peer = std::make_shared<cloud::tcp_peer>(worker_info.worker_id, cloud::DEFAULT_TCP_PORT);
}

worker::~worker()
{
    if (m_tcp_peer)
    {
        m_tcp_peer->stop();
    }
}

void worker::run()
{
    this->download_gltf_file();

    auto &info = m_worker_info;
    auto &instances = m_worker_info.scene_info.instances;

    m_scene.load_scene(m_worker_info.scene_bucket, m_worker_info.scene_root, instances, m_gltf_file_path);

    // Pre-bake the AABB table into geometry::aabb form so ray-AABB
    // pre-filter doesn't reconstruct it on every ray.
    build_aabb_table_cache();

    m_should_terminate = false;

    this->resolution = fvec2((info.max_x - info.min_x) + 1, (info.max_y - info.min_y) + 1);
    this->sample_count = info.samples;
    this->bounce_count = info.bounces;

    // Start TCP peer for direct worker-to-worker communication
    // Expected peers = (num_workers - 1) other workers + 1 master = num_workers
    int expected_peers = m_worker_info.num_workers;
    spdlog::info("Starting TCP peer for worker {} (expecting {} peers)", m_worker_info.worker_id, expected_peers);
    m_tcp_peer->set_ray_callback([this](models::cloud_ray &ray) { process_ray_from_queue(ray); });
    m_tcp_peer->set_terminate_callback([this]() { m_should_terminate = true; });
    m_tcp_peer->start(m_worker_info.cloud_map_namespace, m_worker_info.cloud_map_service,
                      m_worker_info.cloud_map_service_id, expected_peers, m_worker_info.aws_region);

    // Wait for all peers to be discovered and connected before starting work
    if (!m_tcp_peer->wait_for_peers(120))
    {
        spdlog::error("Failed to connect to all peers, aborting");
        return;
    }

    generate_rays();

    unsigned int hardware_threads = std::thread::hardware_concurrency();
    spdlog::info("Hardware Threads: {}", hardware_threads);

    // Reserve threads for: main, TCP IO (2+), tcp_peer flush
    unsigned int tcp_io_threads = 1;
    unsigned int reserved_threads = 1 + tcp_io_threads + 1; // main + TCP IO + tcp flush
    unsigned int available_threads = std::max(5u, hardware_threads - reserved_threads);

    unsigned int shading_threads = std::max(1u, available_threads / 3);
    unsigned int object_intersection_threads = std::max(1u, available_threads / 3);
    unsigned int direct_lighting_intersection_threads = std::max(1u, available_threads / 3);
    unsigned int object_intersection_result_threads = 1;
    unsigned int direct_lighting_intersection_result_threads = 1;

    std::vector<std::thread> threads;

    for (int i = 0; i < object_intersection_threads; i++)
        threads.push_back(std::thread(&worker::process_object_intersections, this));
    for (int i = 0; i < object_intersection_result_threads; i++)
        threads.push_back(std::thread(&worker::process_object_intersection_results, this));

    for (int i = 0; i < direct_lighting_intersection_threads; i++)
        threads.push_back(std::thread(&worker::process_direct_lighting_intersections, this));
    for (int i = 0; i < direct_lighting_intersection_result_threads; i++)
        threads.push_back(std::thread(&worker::process_direct_lighting_intersection_results, this));

    for (int i = 0; i < shading_threads; i++)
        threads.push_back(std::thread(&worker::process_shading, this));

    spdlog::info("Hardware Threads {} Total Threads {}", hardware_threads, threads.size());

    while (!m_should_terminate)
    {
        spdlog::info("Queues: ISECT={}, ISECT_RES={}, DIRECT={}, DIRECT_RES={}, SHADE={}",
                     m_object_intersection_queue.size_approx(), m_object_intersection_result_queue.size_approx(),
                     m_direct_lighting_intersection_queue.size_approx(),
                     m_direct_lighting_intersection_result_queue.size_approx(), m_shading_queue.size_approx());
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    spdlog::info("Termination signal received, stopping worker...");
    m_tcp_peer->stop();

    for (auto &thread : threads)
        thread.join();

    spdlog::info("All worker threads joined, exiting.");
}

void worker::download_gltf_file()
{
    std::string s3_gltf_file{m_worker_info.scene_root + "scene.gltf"};
    std::variant<std::filesystem::path, std::vector<uint8_t>> output{m_gltf_file_path};
    cloud::s3_download_object(m_worker_info.scene_bucket, s3_gltf_file, output);
}

void worker::generate_rays()
{
    using namespace math;

    // Use full image resolution for correct NDC calculation
    fvec2 full_resolution(m_worker_info.image_width, m_worker_info.image_height);
    float ratio = static_cast<float>(full_resolution.x) / full_resolution.y;

    for (uint32_t x = m_worker_info.min_x; x <= m_worker_info.max_x; x++)
    {
        for (uint32_t y = m_worker_info.min_y; y <= m_worker_info.max_y; y++)
        {
            for (uint32_t sample = 0; sample < sample_count; sample++)
            {
                uint64_t uuid = ((uint64_t)x << 40) | ((uint64_t)y << 20) | sample;

                uvec2 pixel(x, y);

                fvec2 aa_offset;
                if (sample == 0 && !transparent_background)
                {
                    aa_offset = fvec2(0, 0);
                }
                else
                {
                    aa_offset = fvec2(core::rand(), core::rand());
                }

                fvec2 ndc = ((fvec2(pixel) + aa_offset) / full_resolution) * 2 - fvec2::one;
                ndc.y = -ndc.y;

                geometry::ray ray = m_scene.m_camera->get_component<scene::camera>()->get_ray(ndc, ratio);

                models::cloud_ray cloud_ray;
                cloud_ray.uuid = uuid;
                cloud_ray.ray = ray;
                cloud_ray.color = fvec4::zero;
                cloud_ray.scale = fvec3::one;
                cloud_ray.bounce = bounce_count;
                cloud_ray.stage = models::ray_stage::INTERSECT;
                cloud_ray.worker_id = m_worker_info.worker_id;

                map_ray_stage_to_queue(cloud_ray);
            }
        }
    }
}

void worker::map_ray_stage_to_queue(const models::cloud_ray &ray)
{
    const models::ray_stage &stage = ray.stage;

    switch (stage)
    {
    case models::ray_stage::INTERSECT:
        m_object_intersection_queue.enqueue(ray);
        break;
    case models::ray_stage::DIRECT_LIGHTING:
        m_direct_lighting_intersection_queue.enqueue(ray);
        break;
    case models::ray_stage::SHADING:
        m_shading_queue.enqueue(ray);
        break;
    case models::ray_stage::ACCUMULATE:
        m_tcp_peer->enqueue_ray(ray, models::MASTER_ID);
        break;
    default:
        break;
    }
}

void worker::process_ray_from_queue(models::cloud_ray &ray)
{
    if (ray.type == models::ray_type::RESOLVE)
    {
        if (ray.stage == models::ray_stage::INTERSECT)
        {
            m_object_intersection_result_queue.enqueue(ray);
        }
        else if (ray.stage == models::ray_stage::DIRECT_LIGHTING)
        {
            m_direct_lighting_intersection_result_queue.enqueue(ray);
        }
    }
    else if (ray.type == models::ray_type::CALCULATE)
    {
        const auto orig_worker = ray.worker_id;

        if (ray.stage == models::ray_stage::INTERSECT)
        {
            calculate_object_intersection(ray);
            ray.worker_id = m_worker_info.worker_id;
        }
        else if (ray.stage == models::ray_stage::DIRECT_LIGHTING)
        {
            calculate_direct_lighting_intersection(ray);
        }

        ray.type = models::ray_type::RESOLVE;
        m_tcp_peer->enqueue_ray(ray, orig_worker);
    }
    else if (ray.type == models::ray_type::OWN)
    {
        map_ray_stage_to_queue(ray);
    }
    else
    {
        spdlog::error("Unknown ray type: {}", static_cast<int>(ray.type));
    }
}

void worker::build_aabb_table_cache()
{
    m_aabb_table_cache.clear();
    m_aabb_table_cache.reserve(m_worker_info.aabb_table.size());
    for (const auto &entry : m_worker_info.aabb_table)
    {
        m_aabb_table_cache.push_back(cached_aabb{
            entry.worker_id,
            geometry::aabb(math::fvec3(entry.min[0], entry.min[1], entry.min[2]),
                           math::fvec3(entry.max[0], entry.max[1], entry.max[2])),
        });
    }
    spdlog::info("AABB table cached: {} workers", m_aabb_table_cache.size());
}

std::vector<std::string> worker::aabb_filter_candidates(const geometry::ray &ray) const
{
    std::vector<std::string> hits;
    hits.reserve(m_aabb_table_cache.size());
    for (const auto &entry : m_aabb_table_cache)
    {
        // Degenerate / empty AABBs (min > max on any axis) are reported
        // as no-hit by geometry::aabb::intersect, so we don't need a
        // special-case for workers that own no geometry.
        if (entry.box.intersect(ray).has_hit())
            hits.push_back(entry.worker_id);
    }
    return hits;
}

} // namespace processors