#pragma once

#include <concurrentqueue/concurrentqueue.h>
#include <mutex>
#include <sys/types.h>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include <path_tracer/geometry/aabb.hpp>
#include <path_tracer/geometry/ray.hpp>

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

    // Build the cached AABB table from m_worker_info.aabb_table. Called
    // once at startup, before any intersection threads are running.
    void build_aabb_table_cache();

    // Returns the worker IDs whose world-space AABB this ray could
    // possibly hit. Used to skip TCP fan-out to workers that own
    // geometry the ray can never touch.
    std::vector<std::string> aabb_filter_candidates(const geometry::ray &ray) const;

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

    std::map<uint64_t, std::pair<int, models::cloud_ray>> m_object_intersection_results;
    std::map<uint64_t, std::pair<int, models::cloud_ray>> m_direct_lighting_intersection_results;

    // Pre-baked (worker_id, geometry::aabb) pairs derived once from the
    // replicated aabb_table in worker_info. Used for ray-AABB pre-filter
    // to decide which peers a ray could possibly hit.
    struct cached_aabb
    {
        std::string worker_id;
        geometry::aabb box;
    };
    std::vector<cached_aabb> m_aabb_table_cache;

    // Expected number of CALCULATE responses we asked for, per ray uuid.
    // Result collectors compare incoming response counts against this
    // (instead of the old hard-coded num_workers) to know when a ray's
    // fan-out is complete.
    //
    // Written by intersection-fan-out threads, read+erased by the result
    // thread, so we need a mutex around each map.
    std::unordered_map<uint64_t, int> m_object_expected_responses;
    std::mutex m_object_expected_mutex;
    std::unordered_map<uint64_t, int> m_direct_lighting_expected_responses;
    std::mutex m_direct_lighting_expected_mutex;
};
} // namespace processors