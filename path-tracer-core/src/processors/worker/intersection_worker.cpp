#include "models/cloud_ray.hpp"
#include "models/intersect_result.hpp"
#include "worker.hpp"
#include <cmath>
#include <path_tracer/core/utils.hpp>
#include <thread>

namespace processors {
    void worker::handle_intersections() {
        while (!m_should_terminate) {
            models::cloud_ray ray{};
            if(!m_intersection_queue.try_dequeue(ray)) {
                std::this_thread::yield();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            auto result = m_scene.intersect(ray.intersect_ray);
            if (ray.stage == models::ray_stage::LIGHTING) {
                ray.direct_light_intersect_result = result.hit;
                m_direct_lighting_result_queue.enqueue(ray);
            }
            else {
                ray.intersect_result = result;
                m_object_intersection_result_queue.enqueue(ray);
            }            
        }
    }

    void worker::handle_object_intersection_results() {
        while (!m_should_terminate) {
            models::cloud_ray ray{};
            if(!m_object_intersection_result_queue.try_dequeue(ray)) {
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
                    auto intersect_result = ray.intersect_result;

                    if (intersect_result.hit && intersect_result.distance < distance) {
                        distance = intersect_result.distance ;
                        closest_intersect_result = intersect_result;
                        hit = true;
                    }
                }

                if (!hit) {
                    models::sample sample{};
                    float alpha = transparent_background ? 0 : 1;
                    auto environment = m_scene.m_environment;
                    if (environment)
                        sample.color = fvec4(fvec3(environment->sample(core::equirectangular_proj(ray.geometry_ray.get_dir()))) * environment_factor, alpha);
                    }
                    else {
                        sample.color = fvec4(fvec3(environment->sample(equirectangular_proj(ray.get_dir()))) * environment_factor, alpha);
                    }

                    sample.scale = fvec3::one;
                    ray.samples.push_back(sample);

                    continue;
                }

                auto sunlight = m_scene.m_sun_light;
                if(sunlight) {
                    fvec3 direct_incoming = sunlight->get_global_transform().basis * fvec3::backward;
			        direct_incoming = util::rand_cone_vec(rand(), math::cos(rand() * sunlight->get_component<scene::sun_light>()->angular_radius),
			                                      direct_incoming);

                    if (math::dot(normal, direct_incoming) > 0) {
                        geometry::ray direct_ray(
                            result.position + direct_incoming * math::epsilon,
                            direct_incoming
                        );

                        ray.intersect_ray = direct_ray;
                    }
                }
                else {
                    
                }
            }
        }
    }
}