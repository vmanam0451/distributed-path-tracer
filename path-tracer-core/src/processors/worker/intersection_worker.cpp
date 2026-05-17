#include <cmath>
#include <mutex>
#include <path_tracer/core/utils.hpp>
#include <thread>

#include "models/cloud_ray.hpp"
#include "models/intersect_result.hpp"
#include "path_tracer/util/rand_cone_vec.hpp"
#include "worker.hpp"

namespace processors
{
void worker::calculate_object_intersection(models::cloud_ray &ray)
{
    auto result = m_scene.intersect_min_result(ray.ray);
    ray.object_intersect_distance = result.distance;

    if (result.hit)
    {
        auto sun_light = m_scene.m_sun_light;
        if (sun_light)
        {
            fvec3 direct_incoming = sun_light->get_global_transform().basis * fvec3::backward;
            direct_incoming = util::rand_cone_vec(
                core::rand(), math::cos(core::rand() * sun_light->get_component<scene::sun_light>()->angular_radius),
                direct_incoming);

            geometry::ray direct_ray(result.position + direct_incoming * math::epsilon, direct_incoming);

            if (math::dot(result.normal, direct_incoming) > 0)
            {
                ray.direct_light_ray = direct_ray;
            }
            else
            {
                ray.direct_light_ray = {};
            }
        }
    }
}

void worker::calculate_direct_lighting_intersection(models::cloud_ray &ray)
{
    bool hit = false;
    if (ray.direct_light_ray.has_value())
    {
        auto result = m_scene.intersect(ray.direct_light_ray.value());
        hit = result.hit;
    }

    ray.direct_light_intersect_result = hit;
}

void worker::process_object_intersections()
{
    constexpr size_t BULK_SIZE = 64;
    models::cloud_ray rays[BULK_SIZE];

    while (!m_should_terminate)
    {
        size_t count = m_object_intersection_queue.try_dequeue_bulk(rays, BULK_SIZE);
        if (count == 0)
        {
            std::this_thread::yield();
            continue;
        }

        for (size_t i = 0; i < count; ++i)
        {
            auto &ray = rays[i];

            // AABB pre-filter. The ray can only hit geometry on workers
            // whose world-space AABB it actually intersects.
            std::vector<std::string> candidates = aabb_filter_candidates(ray.ray);

            if (candidates.empty())
            {
                // No worker's AABB is touched — the ray flies into the
                // environment. Skip the network entirely and forward
                // straight to shading with the max-distance miss marker.
                ray.object_intersect_distance = std::numeric_limits<float>::max();
                ray.direct_light_ray = std::nullopt;
                ray.stage = models::ray_stage::SHADING;
                map_ray_stage_to_queue(ray);
                continue;
            }

            // Record the expected response count BEFORE dispatching, so
            // that even if the first response races back to the result
            // collector immediately, the threshold is already known.
            {
                std::lock_guard<std::mutex> lock(m_object_expected_mutex);
                m_object_expected_responses[ray.uuid] = static_cast<int>(candidates.size());
            }

            bool include_self = false;
            for (const auto &cid : candidates)
            {
                if (cid == m_worker_info.worker_id)
                {
                    include_self = true;
                    continue;
                }
                models::cloud_ray peer_ray = ray;
                peer_ray.type = models::ray_type::CALCULATE;
                m_tcp_peer->enqueue_ray(peer_ray, cid);
            }

            // If our own AABB is in the candidate set, run the local
            // intersection and inject it into the result queue exactly
            // as a peer response would.
            if (include_self)
            {
                calculate_object_intersection(ray);
                m_object_intersection_result_queue.enqueue(ray);
            }
        }
    }
}

void worker::process_direct_lighting_intersections()
{
    constexpr size_t BULK_SIZE = 64;
    models::cloud_ray rays[BULK_SIZE];

    while (!m_should_terminate)
    {
        size_t count = m_direct_lighting_intersection_queue.try_dequeue_bulk(rays, BULK_SIZE);
        if (count == 0)
        {
            std::this_thread::yield();
            continue;
        }

        for (size_t i = 0; i < count; ++i)
        {
            auto &ray = rays[i];

            // No shadow ray (back-facing geometry, sun angle, etc.).
            // Nothing to test — light is unoccluded by definition,
            // advance straight to shading without touching the network.
            if (!ray.direct_light_ray.has_value())
            {
                ray.direct_light_intersect_result = false;
                ray.stage = models::ray_stage::SHADING;
                map_ray_stage_to_queue(ray);
                continue;
            }

            // AABB pre-filter against the shadow ray, same idea as
            // the object intersection path.
            std::vector<std::string> candidates = aabb_filter_candidates(ray.direct_light_ray.value());

            if (candidates.empty())
            {
                // Shadow ray hits no worker's AABB → light is
                // unoccluded → no need to ask anyone.
                ray.direct_light_intersect_result = false;
                ray.stage = models::ray_stage::SHADING;
                map_ray_stage_to_queue(ray);
                continue;
            }

            {
                std::lock_guard<std::mutex> lock(m_direct_lighting_expected_mutex);
                m_direct_lighting_expected_responses[ray.uuid] = static_cast<int>(candidates.size());
            }

            bool include_self = false;
            for (const auto &cid : candidates)
            {
                if (cid == m_worker_info.worker_id)
                {
                    include_self = true;
                    continue;
                }
                models::cloud_ray peer_ray = ray;
                peer_ray.type = models::ray_type::CALCULATE;
                m_tcp_peer->enqueue_ray(peer_ray, cid);
            }

            if (include_self)
            {
                calculate_direct_lighting_intersection(ray);
                m_direct_lighting_intersection_result_queue.enqueue(ray);
            }
        }
    }
}

void worker::process_object_intersection_results()
{
    constexpr size_t BULK_SIZE = 128;
    models::cloud_ray rays[BULK_SIZE];

    while (!m_should_terminate)
    {
        size_t count = m_object_intersection_result_queue.try_dequeue_bulk(rays, BULK_SIZE);
        if (count == 0)
        {
            std::this_thread::yield();
            continue;
        }

        for (size_t i = 0; i < count; ++i)
        {
            auto &ray = rays[i];

            // Look up how many responses we should expect for this ray.
            // This was set by process_object_intersections when it did
            // the AABB pre-filter and dispatched. If we don't find it,
            // a result raced ahead of its bookkeeping entry — that
            // shouldn't happen because we always insert before sending.
            int expected = 0;
            {
                std::lock_guard<std::mutex> lock(m_object_expected_mutex);
                auto eit = m_object_expected_responses.find(ray.uuid);
                if (eit == m_object_expected_responses.end())
                {
                    spdlog::warn("Object result for unknown ray uuid {}", ray.uuid);
                    continue;
                }
                expected = eit->second;
            }

            auto it = m_object_intersection_results.find(ray.uuid);
            if (it == m_object_intersection_results.end())
            {
                m_object_intersection_results[ray.uuid] = std::pair<int, models::cloud_ray>{1, ray};
            }
            else
            {
                auto &results = it->second;
                if (ray.object_intersect_distance < results.second.object_intersect_distance)
                {
                    results = std::pair<int, models::cloud_ray>{results.first + 1, ray};
                }
                else
                {
                    results.first++;
                }
            }

            auto &results = m_object_intersection_results[ray.uuid];
            if (results.first == expected)
            {
                auto best_ray = std::move(results.second);
                if (best_ray.object_intersect_distance == std::numeric_limits<float>::max())
                {
                    best_ray.stage = models::ray_stage::SHADING;
                }
                else
                {
                    if (best_ray.direct_light_ray.has_value())
                    {
                        best_ray.stage = models::ray_stage::DIRECT_LIGHTING;
                    }
                    else
                    {
                        best_ray.stage = models::ray_stage::SHADING;
                    }
                }
                if (best_ray.worker_id == m_worker_info.worker_id)
                {
                    map_ray_stage_to_queue(best_ray);
                }
                else
                {
                    best_ray.type = models::ray_type::OWN;
                    m_tcp_peer->enqueue_ray(best_ray, best_ray.worker_id);
                }

                m_object_intersection_results.erase(ray.uuid);
                {
                    std::lock_guard<std::mutex> lock(m_object_expected_mutex);
                    m_object_expected_responses.erase(ray.uuid);
                }
            }
        }
    }
}

void worker::process_direct_lighting_intersection_results()
{
    constexpr size_t BULK_SIZE = 128;
    models::cloud_ray rays[BULK_SIZE];

    while (!m_should_terminate)
    {
        size_t count = m_direct_lighting_intersection_result_queue.try_dequeue_bulk(rays, BULK_SIZE);
        if (count == 0)
        {
            std::this_thread::yield();
            continue;
        }

        for (size_t i = 0; i < count; ++i)
        {
            auto &ray = rays[i];

            int expected = 0;
            {
                std::lock_guard<std::mutex> lock(m_direct_lighting_expected_mutex);
                auto eit = m_direct_lighting_expected_responses.find(ray.uuid);
                if (eit == m_direct_lighting_expected_responses.end())
                {
                    spdlog::warn("Direct-lighting result for unknown ray uuid {}", ray.uuid);
                    continue;
                }
                expected = eit->second;
            }

            auto it = m_direct_lighting_intersection_results.find(ray.uuid);
            if (it == m_direct_lighting_intersection_results.end())
            {
                m_direct_lighting_intersection_results[ray.uuid] = std::pair<int, models::cloud_ray>{1, ray};
            }
            else
            {
                auto &results = it->second;
                if (ray.direct_light_intersect_result)
                {
                    results = std::pair<int, models::cloud_ray>{results.first + 1, ray};
                }
                else
                {
                    results.first++;
                }
            }

            auto &results = m_direct_lighting_intersection_results[ray.uuid];
            if (results.first == expected)
            {
                auto best_ray = std::move(results.second);
                best_ray.stage = models::ray_stage::SHADING;
                map_ray_stage_to_queue(best_ray);

                m_direct_lighting_intersection_results.erase(ray.uuid);
                {
                    std::lock_guard<std::mutex> lock(m_direct_lighting_expected_mutex);
                    m_direct_lighting_expected_responses.erase(ray.uuid);
                }
            }
        }
    }
}
} // namespace processors