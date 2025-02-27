#include "models/cloud_ray.hpp"
#include "worker.hpp"
#include <path_tracer/math/vec3.hpp>
#include <path_tracer/util/rand_cone_vec.hpp>
#include <path_tracer/core/pbr.hpp>

namespace processors {

    void worker::process_direct_lighting() {
        using namespace math;

        while (!m_should_terminate) {
            models::cloud_ray ray{};
            if(!m_direct_lighting_queue.try_dequeue(ray)) {
                std::this_thread::yield();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            auto result = ray.direct_light_intersect_result;
            auto geometry_ray = ray.geometry_ray;

            fvec3 albedo = result.albedo;
		    float opacity = result.opacity;
		    float roughness = result.roughness;
		    float metallic = result.metallic;
		    float ior = result.ior;
            fvec3 normal = result.normal;

            if (!is_approx(opacity, 1) && rand() > opacity) {
                geometry::ray opacity_ray(
                    result.position + geometry_ray.get_dir() * epsilon,
                    geometry_ray.get_dir()
                );

                models::cloud_ray opacity_cloud_ray = ray;
                opacity_cloud_ray.geometry_ray = opacity_ray;
                opacity_cloud_ray.intersect_ray = opacity_ray;
                
                opacity_cloud_ray.stage = models::ray_stage::INITIAL;
                map_ray_stage_to_queue(opacity_cloud_ray);
            }
            else {
                auto sunlight = m_scene.m_sun_light;
                if (sunlight) {
                    fvec3 direct_incoming = sunlight->get_global_transform().basis * fvec3::backward;
			        direct_incoming = util::rand_cone_vec(rand(), cos(rand() * sunlight->get_component<scene::sun_light>()->angular_radius),
			                                        direct_incoming);
                    if (dot(normal, direct_incoming) > 0) {
                        geometry::ray direct_ray(
                            result.position + direct_incoming * epsilon,
                            direct_incoming
                        );

                        ray.intersect_ray = direct_ray;
                        m_intersection_queue.enqueue(ray);
                    }

                }
                else {
                    ray.stage = models::ray_stage::INDIRECT_LIGHTING;
                    map_ray_stage_to_queue(ray);
                }
            }
        }
    }

    void worker::process_direct_lighting_results() {
        using namespace core;
        using namespace math;

        while (!m_should_terminate) {
            models::cloud_ray ray{};
            if(!m_direct_lighting_result_queue.try_dequeue(ray)) {
                std::this_thread::yield();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            auto direct_intersect = ray.direct_light_intersect_result;
            auto object_intersect = ray.object_intersect_result;
            auto sunlight = m_scene.m_sun_light;
            
            fvec3 direct_incoming = sunlight->get_global_transform().basis * fvec3::backward;
			direct_incoming = util::rand_cone_vec(rand(), cos(rand() * sunlight->get_component<scene::sun_light>()->angular_radius),
			                                        direct_incoming);
            fvec3 outcoming = -ray.geometry_ray.get_dir();
            
            fvec3 direct_out;

            if(!direct_intersect.hit) {
                if(object_intersect.shadow_catcher && ray.bounce == bounce_count) {
                    geometry::ray opacity_ray(
                        object_intersect.position + ray.geometry_ray.get_dir() * epsilon,
                        ray.geometry_ray.get_dir()
                    );

                    models::cloud_ray opacity_cloud_ray = ray;

                    opacity_cloud_ray.geometry_ray = opacity_ray;
                    opacity_cloud_ray.intersect_ray = opacity_ray;
                    opacity_cloud_ray.stage = models::ray_stage::INITIAL;
                    map_ray_stage_to_queue(opacity_cloud_ray);
                    continue;
                }

                float diffuse_pdf = core::pbr::pdf_diffuse(object_intersect.normal, direct_incoming);
				fvec3 diffuse_brdf = diffuse_pdf * object_intersect.albedo;

                float specular_pdf = pbr::pdf_specular(object_intersect.normal, outcoming, direct_incoming, object_intersect.roughness);
				fvec3 specular_brdf(specular_pdf);

                fvec3 fresnel = lerp(fvec3(0.04F), object_intersect.albedo, object_intersect.metallic);
				{
					fvec3 halfway = normalize(outcoming + direct_incoming);
					float cos_theta = dot(outcoming, halfway);

					fresnel = lerp(fresnel, fvec3::one, math::pow(1 - cos_theta, 5));
				}

                diffuse_brdf = lerp(diffuse_brdf, fvec3::zero, object_intersect.metallic);
				fvec3 brdf = lerp(diffuse_brdf, specular_brdf, fresnel);

                diffuse_pdf = 1;
                specular_pdf = 1;

                float specular_probability = pbr::fresnel(outcoming, reflect(-outcoming, object_intersect.normal), object_intersect.ior);
		        specular_probability = math::max(specular_probability, object_intersect.metallic);

				float pdf = lerp(diffuse_pdf, specular_pdf, specular_probability);

                fvec3 direct_in = m_scene.m_sun_light->get_component<scene::sun_light>()->energy;
				
                direct_out = brdf * direct_in / math::max(pdf, math::epsilon);
				direct_out = math::clamp(direct_out, fvec3::zero, direct_in);

                ray.color += fvec4(direct_out, 1);
                ray.stage = models::ray_stage::INDIRECT_LIGHTING;
                map_ray_stage_to_queue(ray);
            }
            else {
                if (object_intersect.shadow_catcher && ray.bounce == bounce_count) {
                    ray.stage = models::ray_stage::COMPLETED;
                    ray.color += fvec4::future;

                    map_ray_stage_to_queue(ray);
                    continue;
                }
            }
        }
    }
}