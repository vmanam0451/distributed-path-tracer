#include "worker.hpp"
#include <path_tracer/math/vec3.hpp>
#include <path_tracer/util/rand_cone_vec.hpp>

namespace processors {

    void worker::process_direct_lighting() {
        while (!m_should_terminate) {
            models::cloud_ray cloud_ray{};
            if(!m_direct_lighting_queue.try_dequeue(cloud_ray)) {
                std::this_thread::yield();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            auto result = cloud_ray.intersect_result;
            auto ray = cloud_ray.geometry_ray;

            math::fvec3 albedo = result.albedo;
		    float opacity = result.opacity;
		    float roughness = result.roughness;
		    float metallic = result.metallic;
		    float ior = result.ior;
            math::fvec3 normal = result.normal;

            if (!math::is_approx(opacity, 1) && rand() > opacity) {
                geometry::ray opacity_ray(
                    result.position + ray.get_dir() * math::epsilon,
                    ray.get_dir()
                );

                models::cloud_ray opacity_cloud_ray = cloud_ray;
                opacity_cloud_ray.stage = models::ray_stage::INITIAL;

                map_ray_stage_to_queue(opacity_cloud_ray);
            }
            else {
                auto sunlight = m_scene.m_sun_light;
                if (sunlight) {
                    math::fvec3 direct_incoming = sunlight->get_global_transform().basis * math::fvec3::backward;
			        direct_incoming = util::rand_cone_vec(rand(), math::cos(rand() * sunlight->get_component<scene::sun_light>()->angular_radius),
			                                        direct_incoming);
                    if (math::dot(normal, direct_incoming) > 0) {
                        geometry::ray direct_ray(
                            result.position + direct_incoming * math::epsilon,
                            direct_incoming
                        );

                        cloud_ray.geometry_ray = direct_ray;

                        m_intersection_queue.enqueue()
                    }

                }
                else {
                    cloud_ray.stage = models::ray_stage::INDIRECT_LIGHTING;
                    map_ray_stage_to_queue(cloud_ray);
                }
            }
        }
    }

    void worker::process_direct_lighting_results() {

    }
}