#include "models/cloud_ray.hpp"
#include "models/intersect_result.hpp"
#include "path_tracer/util/rand_cone_vec.hpp"
#include "worker.hpp"
#include <cmath>
#include <path_tracer/core/utils.hpp>
#include <thread>

namespace processors {
    void worker::process_object_intersections() {
        while (!m_should_terminate) {
            models::cloud_ray ray{};
            if(!m_object_intersection_queue.try_dequeue(ray)) {
                std::this_thread::yield();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            auto result = m_scene.intersect_min_result(ray.ray);
            ray.object_intersect_distance = result.distance;

            if (result.hit) {
                auto sun_light = m_scene.m_sun_light;
                if (sun_light) {
                    fvec3 direct_incoming = sun_light->get_global_transform().basis * fvec3::backward;
			        direct_incoming = util::rand_cone_vec(core::rand(), math::cos(core::rand() * sun_light->get_component<scene::sun_light>()->angular_radius),
			                                      direct_incoming);

                    geometry::ray direct_ray(
                        result.position + direct_incoming * math::epsilon,
                        direct_incoming);

                    if (math::dot(result.normal, direct_incoming) > 0) {
                        ray.direct_light_ray = direct_ray;
                    }
                    else {
                        ray.direct_light_ray = {};
                    }
                }
            }

            m_object_intersection_result_queue.enqueue(ray);
        }
    }

    void worker::process_direct_lighting_intersections() {
        while (!m_should_terminate) {
            models::cloud_ray ray{};
            if(!m_direct_lighting_intersection_queue.try_dequeue(ray)) {
                std::this_thread::yield();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            bool hit = false;
            if (ray.direct_light_ray.has_value()) {
                auto result = m_scene.intersect(ray.direct_light_ray.value());
                hit = result.hit;
            }

            ray.direct_light_intersect_result = hit;
            m_direct_lighting_intersection_result_queue.enqueue(ray);
        }
    }

    void worker::process_object_intersection_results() {
        while (!m_should_terminate) {
            models::cloud_ray ray{};
            if(!m_object_intersection_result_queue.try_dequeue(ray)) {
                std::this_thread::yield();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            if(!m_object_intersection_results.contains(ray.uuid)) {
                m_object_intersection_results[ray.uuid] = std::pair<int, models::cloud_ray>{1, ray};
            }

            auto results = m_object_intersection_results[ray.uuid];
            if(results.first < m_worker_info.num_workers) {
                auto previous_best = results.second;
                if (ray.object_intersect_distance < previous_best.object_intersect_distance) {
                    m_object_intersection_results[ray.uuid] = std::pair<int, models::cloud_ray>{results.first + 1, ray};
                }
                else {
                    m_object_intersection_results[ray.uuid] = std::pair<int, models::cloud_ray>{results.first + 1, previous_best};
                }
            }
            
            results = m_object_intersection_results[ray.uuid];
            if (results.first == m_worker_info.num_workers) {
                m_object_intersection_results.erase(ray.uuid);
                
                auto best_ray = results.second;
                if (best_ray.object_intersect_distance == std::numeric_limits<float>::max()) {
                    best_ray.stage = models::ray_stage::SHADING;
                }
                else {
                    if (best_ray.direct_light_ray.has_value()) {
                        best_ray.stage = models::ray_stage::DIRECT_LIGHTING;
                    }
                    else {
                        best_ray.stage = models::ray_stage::SHADING;
                    }
                }
                map_ray_stage_to_queue(best_ray);
            }
        }
    }

    void worker::process_direct_lighting_intersection_results() {
        while (!m_should_terminate) {
            models::cloud_ray ray{};
            if(!m_direct_lighting_intersection_result_queue.try_dequeue(ray)) {
                std::this_thread::yield();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            if(!m_direct_lighting_intersection_results.contains(ray.uuid)) {
                m_direct_lighting_intersection_results[ray.uuid] = std::pair<int, models::cloud_ray>{1, ray};
            }

            auto results = m_direct_lighting_intersection_results[ray.uuid];
            if (results.first < m_worker_info.num_workers) {
                auto previous = results.second;
                if (ray.direct_light_intersect_result) {
                    m_direct_lighting_intersection_results[ray.uuid] = std::pair<int, models::cloud_ray>{results.first + 1, ray};
                }
                else {
                    m_direct_lighting_intersection_results[ray.uuid] = std::pair<int, models::cloud_ray>{results.first + 1, previous};
                }
            }
            
            results = m_direct_lighting_intersection_results[ray.uuid];
            if (results.first == m_worker_info.num_workers) {
                m_direct_lighting_intersection_results.erase(ray.uuid);
                
                auto best_ray = results.second;
                best_ray.stage = models::ray_stage::SHADING;
                map_ray_stage_to_queue(best_ray);
            }
        }
    }
}