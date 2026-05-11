#pragma once

#include <concurrentqueue/concurrentqueue.h>
#include <mutex>
#include <sys/types.h>
#include <unordered_map>

#include <cstdint>
#include <memory>

#include "cloud/s3.hpp"
#include "cloud/tcp_peer.hpp"
#include "models/cloud_ray.hpp"
#include "models/work_info.hpp"
#include "pch.hpp"
#include "processors/application.hpp"
#include "scene/scene.hpp"

namespace processors
{
class worker : public application
{
  public:
    worker(const models::worker_info &worker_info);
    void run() override;
    ~worker() override;

  public:
    math::uvec2 resolution = math::fvec2(640, 480);
    uint32_t sample_count = 50;
    math::fvec3 environment_factor = math::fvec3::one;
    bool transparent_background = false;
    uint8_t bounce_count = 10;

  private:
    void download_gltf_file();

    void generate_rays();
    void map_ray_stage_to_queue(const models::cloud_ray &ray);

    void process_object_intersections();
    void process_object_intersection_results();

    void process_direct_lighting_intersections();
    void process_direct_lighting_intersection_results();

    void process_shading();

    void calculate_object_intersection(models::cloud_ray &ray);
    void calculate_direct_lighting_intersection(models::cloud_ray &ray);

    void process_ray_from_queue(models::cloud_ray &ray);

    std::vector<uint8_t> render() const;

    // TODO:
    /*
        Simplify lifecycle.
        Should Only need 3.
            - Intersection w/ object
            - Intersection w/ direct light
            - Accumulate
        Test with this simplified lifecycle.
        If not working, test with keeping track of samples and accumulating them
       in the complete stage i.e ray has vector<samples> sample={direct light,
       emissive, scale} If not working, test intersection with renderer. Call
       scene.intersect inside renderer and make sure that is working
    */

  private:
    models::worker_info m_worker_info;
    std::filesystem::path m_gltf_file_path;
    cloud::distributed_scene m_scene;

    std::atomic<bool> m_should_terminate;

    // TCP peer for direct worker-to-worker communication
    std::shared_ptr<cloud::tcp_peer> m_tcp_peer;

    moodycamel::ConcurrentQueue<models::cloud_ray> m_object_intersection_queue;
    moodycamel::ConcurrentQueue<models::cloud_ray> m_object_intersection_result_queue;

    moodycamel::ConcurrentQueue<models::cloud_ray> m_direct_lighting_intersection_queue;
    moodycamel::ConcurrentQueue<models::cloud_ray> m_direct_lighting_intersection_result_queue;

    moodycamel::ConcurrentQueue<models::cloud_ray> m_shading_queue;

    std::unordered_map<uint64_t, std::pair<int, models::cloud_ray>> m_object_intersection_results;
    std::unordered_map<uint64_t, std::pair<int, models::cloud_ray>> m_direct_lighting_intersection_results;
};
} // namespace processors