#include "models/cloud_ray.hpp"
#include "worker.hpp"
#include <path_tracer/math/vec3.hpp>
#include <path_tracer/util/rand_cone_vec.hpp>
#include <path_tracer/core/pbr.hpp>
#include <path_tracer/core/utils.hpp>

namespace processors {

    void worker::process_shading() {
        using namespace math;
        using namespace core;

        while (!m_should_terminate) {
            models::cloud_ray ray{};
            if(!m_shading_queue.try_dequeue(ray)) {
                std::this_thread::yield();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            geometry::ray& current_ray = ray.ray;
            fvec3& accumulated_color = ray.color;
            fvec3& throughput = ray.scale;
            float& alpha = ray.alpha;

            auto result = m_scene.intersect(current_ray);
            if (!result.hit) {
                if (m_scene.m_environment) {
                    fvec3 env_color = fvec3(m_scene.m_environment->sample(
                        core::equirectangular_proj(current_ray.get_dir()))) * environment_factor;
                    accumulated_color += throughput * env_color;
                } else {
                    accumulated_color += throughput * environment_factor;
                }
                alpha = transparent_background ? 0.0f : 1.0f;
                
                ray.stage = models::ray_stage::ACCUMULATE;
                map_ray_stage_to_queue(ray);
                continue;
            }

            alpha = 1.0f;

            fvec3 albedo = result.material->get_albedo(result.tex_coord);
            float opacity = result.material->get_opacity(result.tex_coord);
            float roughness = result.material->get_roughness(result.tex_coord);
            float metallic = result.material->get_metallic(result.tex_coord);
            fvec3 emissive = result.material->get_emissive(result.tex_coord) * 10;
            float ior = result.material->ior;

            accumulated_color += throughput * emissive;

            if (!math::is_approx(opacity, 1) && core::rand() > opacity) {
                current_ray = geometry::ray(
                    result.position + current_ray.get_dir() * math::epsilon,
                    current_ray.get_dir()
                );

                ray.stage = models::ray_stage::SHADING;
                map_ray_stage_to_queue(ray);
                continue; 
            }

            fvec3 normal = result.get_normal();
            fvec3 outcoming = -current_ray.get_dir();

            if (math::dot(normal, outcoming) <= 0) {
                ray.stage = models::ray_stage::ACCUMULATE;
                map_ray_stage_to_queue(ray);
                continue;
            }

            if (result.material->shadow_catcher && ray.bounce == bounce_count) {
                bool in_shadow = true;
               
                if (m_scene.m_sun_light) {
                    fvec3 direct_incoming = m_scene.m_sun_light->get_global_transform().basis * fvec3::backward;
                    direct_incoming = util::rand_cone_vec(
                        core::rand(), 
                        math::cos(core::rand() * m_scene.m_sun_light->get_component<scene::sun_light>()->angular_radius),
                        direct_incoming
                    );

                    if (math::dot(normal, direct_incoming) > 0) {
                        geometry::ray shadow_ray(
                            result.position + direct_incoming * math::epsilon,
                            direct_incoming
                        );
                        auto shadow_result = m_scene.intersect(shadow_ray);

                        if (!shadow_result.hit) {
                            in_shadow = false;
                        }
                    }
                }
                if (in_shadow) {
                    ray.color = fvec3::zero;
                    ray.alpha = 1;
                    ray.stage = models::ray_stage::ACCUMULATE;
                    map_ray_stage_to_queue(ray);
                    continue;
                } else {
                    current_ray = geometry::ray(
                        result.position + current_ray.get_dir() * math::epsilon,
                        current_ray.get_dir()
                    );

                    ray.stage = models::ray_stage::SHADING;
                    map_ray_stage_to_queue(ray);
                    continue;
                }
            }

            roughness = math::max(roughness, 0.05F);
            float specular_probability = core::pbr::fresnel(outcoming, reflect(-outcoming, normal), ior);
            specular_probability = math::max(specular_probability, metallic);
            bool specular_sample = core::rand() < specular_probability;

            if (m_scene.m_sun_light) {
                fvec3 direct_incoming = m_scene.m_sun_light->get_global_transform().basis * fvec3::backward;
                direct_incoming = util::rand_cone_vec(
                    core::rand(), 
                    math::cos(core::rand() * m_scene.m_sun_light->get_component<scene::sun_light>()->angular_radius),
                    direct_incoming
                );

                if (math::dot(normal, direct_incoming) > 0) {
                    geometry::ray direct_ray(
                        result.position + direct_incoming * math::epsilon,
                        direct_incoming
                    );
                    auto direct_result = m_scene.intersect(direct_ray);
                    
                    if (!direct_result.hit) {
                        float diffuse_pdf = pbr::pdf_diffuse(normal, direct_incoming);
                        fvec3 diffuse_brdf = diffuse_pdf * albedo;
                        
                        float specular_pdf = pbr::pdf_specular(normal, outcoming, direct_incoming, roughness);
                        fvec3 specular_brdf(specular_pdf);
                        
                        fvec3 fresnel = lerp(fvec3(0.04F), albedo, metallic);
                        {
                            fvec3 halfway = normalize(outcoming + direct_incoming);
                            float cos_theta = dot(outcoming, halfway);
                            fresnel = lerp(fresnel, fvec3::one, math::pow(1 - cos_theta, 5));
                        }
                        
                        diffuse_brdf = lerp(diffuse_brdf, fvec3::zero, metallic);
                        fvec3 brdf = lerp(diffuse_brdf, specular_brdf, fresnel);
                        
                        diffuse_pdf = 1;
                        specular_pdf = 1;
                        float pdf = lerp(diffuse_pdf, specular_pdf, specular_probability);
                        
                        fvec3 direct_in = m_scene.m_sun_light->get_component<scene::sun_light>()->energy;
                        fvec3 direct_out = brdf * direct_in / math::max(pdf, math::epsilon);
                        direct_out = math::clamp(direct_out, fvec3::zero, direct_in);
                        
                        accumulated_color += throughput * direct_out;
                    }
                }
            
            }

            fvec2 rand_val(core::rand(), core::rand());
            fvec3 indirect_incoming = specular_sample
                ? pbr::importance_specular(rand_val, normal, outcoming, roughness)
                : pbr::importance_diffuse(rand_val, normal, outcoming);
            
            if (math::dot(normal, indirect_incoming) > 0) {
                float diffuse_pdf = pbr::pdf_diffuse(normal, indirect_incoming);
                fvec3 diffuse_brdf = diffuse_pdf * albedo;
                
                float specular_pdf = pbr::pdf_specular(normal, outcoming, indirect_incoming, roughness);
                fvec3 specular_brdf(specular_pdf);
                
                fvec3 fresnel = lerp(fvec3(0.04F), albedo, metallic);
                {
                    fvec3 halfway = normalize(outcoming + indirect_incoming);
                    float cos_theta = dot(outcoming, halfway);
                    fresnel = lerp(fresnel, fvec3::one, math::pow(1 - cos_theta, 5));
                }
                
                diffuse_brdf = lerp(diffuse_brdf, fvec3::zero, metallic);
                fvec3 brdf = lerp(diffuse_brdf, specular_brdf, fresnel);
                
                float pdf = lerp(diffuse_pdf, specular_pdf, specular_probability);
                
                throughput *= brdf / math::max(pdf, math::epsilon);
                
                throughput = math::clamp(throughput, fvec3::zero, fvec3(10.0f));
                
                current_ray = geometry::ray(
                    result.position + indirect_incoming * math::epsilon,
                    indirect_incoming
                );

                if (ray.bounce < bounce_count - 2) {
                    float p = math::max(throughput.x, math::max(throughput.y, throughput.z));
                    if (core::rand() > p) {
                        ray.stage = models::ray_stage::ACCUMULATE;
                        map_ray_stage_to_queue(ray);
                        continue;
                    }
                    throughput /= p; // Compensate for termination
                }

                ray.bounce -= 1;
                ray.stage = ray.bounce > 0 ? models::ray_stage::SHADING : models::ray_stage::ACCUMULATE;
                map_ray_stage_to_queue(ray);
            }
            else {
                ray.stage = models::ray_stage::ACCUMULATE;
                map_ray_stage_to_queue(ray);
            }
        }
    }
}