#include "worker.hpp"

#include <cstdint>
#include <path_tracer/core/pbr.hpp>
#include <path_tracer/core/renderer.hpp>
#include <path_tracer/core/utils.hpp>
#include <path_tracer/image/image.hpp>
#include <path_tracer/math/vec2.hpp>
#include <path_tracer/math/vec3.hpp>
#include <path_tracer/util/thread_pool.hpp>

#include "cloud/messaging.hpp"
#include "cloud/s3.hpp"
#include "models/cloud_ray.hpp"
#include "models/messaging.hpp"
#include "path_tracer/util/rand_cone_vec.hpp"

namespace processors
{
worker::worker(const models::worker_info &worker_info)
{
    this->m_worker_info = worker_info;
    this->m_gltf_file_path = std::filesystem::path("/tmp/scene.gltf");
}

worker::~worker()
{
}

void worker::run()
{
    this->download_gltf_file();

    auto &info = m_worker_info;
    auto &work = m_worker_info.scene_info.work;

    m_scene.load_scene(m_worker_info.scene_bucket, m_worker_info.scene_root, work, m_gltf_file_path);

    m_should_terminate = false;

    this->resolution = fvec2((info.max_x - info.min_x), (info.max_y - info.min_y));
    this->sample_count = info.samples;
    this->bounce_count = info.bounces;

    generate_rays();

    unsigned int hardware_threads = std::thread::hardware_concurrency();
    unsigned int available_threads = std::max(1u, hardware_threads - 3);

    unsigned int shading_threads = std::max(1u, static_cast<unsigned int>(std::ceil(available_threads * 0.4)));
    unsigned int object_intersection_threads = std::max(1u, static_cast<unsigned int>(std::ceil(available_threads * 0.15)));
    unsigned int direct_lighting_intersection_threads = std::max(1u, static_cast<unsigned int>(std::ceil(available_threads * 0.15)));
    unsigned int object_intersection_result_threads = std::max(1u, static_cast<unsigned int>(std::ceil(available_threads * 0.15)));
    unsigned int direct_lighting_intersection_result_threads = std::max(1u, static_cast<unsigned int>(std::ceil(available_threads * 0.15)));

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

    threads.push_back(std::thread([&]() {
        while (!m_should_terminate)
        {
            spdlog::info("Queue sizes: INTERSECT={}, INTERSECT_RESULT={}, DIRECT={}, "
                         "DIRECT_RESULT={}, INDIRECT={},",
                         m_object_intersection_queue.size_approx(), m_object_intersection_result_queue.size_approx(),
                         m_direct_lighting_intersection_queue.size_approx(),
                         m_direct_lighting_intersection_result_queue.size_approx(), m_shading_queue.size_approx());
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }));

    models::SQSOptions sqs_options;
    sqs_options.queueUrl = m_worker_info.sqs_queue_url;

    threads.push_back(std::thread([&]() {
        cloud::sqs_poll(sqs_options, m_should_terminate, [&](models::cloud_ray &ray) { process_ray_from_queue(ray); });
    }));

    spdlog::info("Hardware Threads {} Total Threads {},", hardware_threads, threads.size());

    for (auto &thread : threads)
        thread.join();
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

    for (uint32_t x = m_worker_info.min_x; x < m_worker_info.max_x; x++)
    {
        for (uint32_t y = m_worker_info.min_y; y < m_worker_info.max_y; y++)
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

                fvec2 ndc = ((fvec2(pixel) + aa_offset) / resolution) * 2 - fvec2::one;
                ndc.y = -ndc.y;
                float ratio = static_cast<float>(resolution.x) / resolution.y;

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
        if (ray.worker_id == m_worker_info.worker_id)
        {
            return;
        }

        if (ray.stage == models::ray_stage::INTERSECT)
        {
            calculate_object_intersection(ray);
        }
        else if (ray.stage == models::ray_stage::DIRECT_LIGHTING)
        {
            calculate_direct_lighting_intersection(ray);
        }

        const auto orig_worker = ray.worker_id;
        ray.worker_id = m_worker_info.worker_id;

        ray.type = models::ray_type::RESOLVE;
        cloud::sns_send(m_worker_info.sns_topic_arn, orig_worker, ray);
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

} // namespace processors