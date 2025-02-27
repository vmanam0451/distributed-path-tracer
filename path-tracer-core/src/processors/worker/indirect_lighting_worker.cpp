#include "models/cloud_ray.hpp"
#include "worker.hpp"
#include <path_tracer/math/vec3.hpp>
#include <path_tracer/util/rand_cone_vec.hpp>
#include <path_tracer/core/pbr.hpp>
#include <path_tracer/core/utils.hpp>

namespace processors {

    void worker::process_indirect_lighting_results() {
        using namespace math;
        using namespace core;

        while (!m_should_terminate) {
            models::cloud_ray ray{};
            if(!m_direct_lighting_queue.try_dequeue(ray)) {
                std::this_thread::yield();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            auto result = ray.object_intersect_result;
            fvec3 normal = result.normal;
		    fvec3 outcoming = -ray.geometry_ray.get_dir();

            fvec3 albedo = result.albedo;
		    float opacity = result.opacity;
		    float roughness = result.roughness;
		    float metallic = result.metallic;
		    fvec3 emissive = result.emissive * 10;

            float specular_probability = pbr::fresnel(outcoming, reflect(-outcoming, normal), ior);
		    specular_probability = math::max(specular_probability, metallic);
		    bool specular_sample = core::rand() < specular_probability;

            fvec3 indirect_out;
            fvec2 rand(core::rand(), core::rand());
		    fvec3 indirect_incoming = specular_sample
			                          ? pbr::importance_specular(rand, normal, outcoming, roughness)
			                          : pbr::importance_diffuse(rand, normal, outcoming);
            
            if(math::dot(normal, indirect_incoming) > 0) {
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

                fvec3 scale = brdf / math::max(pdf, math::epsilon);
                ray.scale *= scale;

                indirect_out = ray.scale * emissive;
                ray.color += fvec4(indirect_out, 1);

                uint8_t bounce = ray.bounce - 1;

                if (bounce == 0) {
                    ray.stage = models::ray_stage::COMPLETED;
                    map_ray_stage_to_queue(ray);
                    continue;
                }

                geometry::ray indirect_ray(
                    result.position + indirect_incoming * math::epsilon,
                    indirect_incoming
                );

                models::cloud_ray cloud_indirect_ray = ray;

                cloud_indirect_ray.geometry_ray = indirect_ray;
                cloud_indirect_ray.intersect_ray = indirect_ray;
                cloud_indirect_ray.bounce = ray.bounce - 1;
                cloud_indirect_ray.stage = models::ray_stage::INITIAL;

                map_ray_stage_to_queue(cloud_indirect_ray);
            }

            else {
                ray.stage = models::ray_stage::COMPLETED;
                map_ray_stage_to_queue(ray);
            }
        }
    }
}
