#include "models/intersect_result.hpp"
#include "worker.hpp"
#include <cmath>
#include <path_tracer/core/utils.hpp>
#include <thread>

namespace processors {
    void worker::process_intersections() {
        while (!m_should_terminate) {
            models::cloud_ray ray{};
            if(!m_intersection_queue.try_dequeue(ray)) {
                std::this_thread::yield();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            auto result = m_scene.intersect(ray.intersect_ray);
            if (ray.stage == models::ray_stage::DIRECT_LIGHTING) {
                ray.direct_light_intersect_result = result;
            }
            else {
                ray.object_intersect_result = result;
            }
            
            m_intersection_result_queue.enqueue(ray);
        }
    }

    void worker::process_intersection_results() {
        auto get_intersect_result = [](const models::cloud_ray& ray) {
            return ray.stage == models::ray_stage::DIRECT_LIGHTING ? ray.direct_light_intersect_result : ray.object_intersect_result;
        };
        
        while (!m_should_terminate) {
            models::cloud_ray ray{};
            if(!m_intersection_result_queue.try_dequeue(ray)) {
                std::this_thread::yield();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            if(!m_intersection_results.contains(ray.uuid)) {
                m_intersection_results[ray.uuid] = std::vector<models::cloud_ray>{};
            }

            m_intersection_results[ray.uuid].push_back(ray);

            if(m_intersection_results[ray.uuid].size() == m_worker_info.num_workers) {
                float distance = std::numeric_limits<float>::max();
                bool hit = false;
                std::vector<models::cloud_ray> results = m_intersection_results[ray.uuid];
                models::intersect_result closest_intersect_result = {false};
                
                for (const models::cloud_ray& result_ray : results) {
                    auto intersect_result = get_intersect_result(result_ray);

                    if (intersect_result.hit && intersect_result.distance < distance) {
                        distance = intersect_result.distance ;
                        closest_intersect_result = intersect_result;
                        hit = true;
                    }
                }

                m_intersection_results.erase(ray.uuid);

                if (!hit && ray.stage == models::ray_stage::INITIAL) {
                    auto environment = m_scene.m_environment;
                    math::fvec4 color;
                    float alpha = transparent_background ? 0 : 1;
                    if (m_scene.m_environment) {
                        color = fvec4(fvec3(environment->sample(core::equirectangular_proj(ray.geometry_ray.get_dir()))) * environment_factor, alpha);
                    }
                    else {
                        color = math::fvec4(environment_factor, alpha);
                    }

                    ray.stage = models::ray_stage::COMPLETED;
                    ray.color += color;
                    
                    map_ray_stage_to_queue(ray);
                    continue;
                }
                
                if (ray.stage == models::ray_stage::INITIAL) {
                    fvec3 normal = closest_intersect_result.normal;
                    fvec3 outcoming = -ray.geometry_ray.get_dir();

                    if (math::dot(normal, outcoming) <= 0) {
                        ray.stage = models::ray_stage::COMPLETED;
                        map_ray_stage_to_queue(ray);
                        continue;
                    }
                }

                if (ray.stage == models::ray_stage::DIRECT_LIGHTING) {
                    ray.direct_light_intersect_result = closest_intersect_result;
                }
                else {
                    ray.object_intersect_result = closest_intersect_result;
                }

                ray.stage = static_cast<models::ray_stage>(static_cast<int>(ray.stage) + 1);
                map_ray_stage_to_queue(ray);   
            }
        }
    }
}