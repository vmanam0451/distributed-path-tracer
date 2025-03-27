// #include "models/cloud_ray.hpp"
// #include "worker.hpp"
// #include <path_tracer/math/vec3.hpp>
// #include <path_tracer/util/rand_cone_vec.hpp>
// #include <path_tracer/core/pbr.hpp>
// #include <path_tracer/core/utils.hpp>

// namespace processors {

//     void worker::process_shading() {
//         using namespace math;
//         using namespace core;

//         while (!m_should_terminate) {
//             models::cloud_ray ray{};
//             if(!m_shading_queue.try_dequeue(ray)) {
//                 std::this_thread::yield();
//                 std::this_thread::sleep_for(std::chrono::milliseconds(1));
//                 continue;
//             }

//             auto object_intersect_distance = ray.object_intersect_distance;
//             if (object_intersect_distance == std::numeric_limits<float>::max()) {
//                 fvec4 color;
                
//                 float alpha = transparent_background ? 0 : 1;
//                 auto environment = m_scene.m_environment;
//                 if (environment) {
//                     color = fvec4(fvec3(environment->sample(equirectangular_proj(ray.ray.get_dir()))) * environment_factor, alpha);
//                 }
//                 else {
//                     color = fvec4(environment_factor, alpha);
//                 }

                
//                 color = fvec3(color) * ray.scale;
//                 ray.color += color;

//                 ray.stage = models::ray_stage::ACCUMULATE;
//                 map_ray_stage_to_queue(ray);
//                 continue;
//             }

//             auto result = m_scene.intersect(ray.ray);
            
//             fvec3 albedo = result.albedo;
// 		    float opacity = result.opacity;
// 		    float roughness = result.roughness;
// 		    float metallic = result.metallic;
// 		    fvec3 emissive = result.emissive * 10; // DEBUG
// 		    float ior = result.ior;

//             if (!math::is_approx(opacity, 1) && core::rand() > opacity) {
//                 geometry::ray opacity_ray(
//                     result.position + ray.ray.get_dir() * math::epsilon,
//                     ray.ray.get_dir()
//                 );
                
//                 ray.ray = opacity_ray;
//                 ray.direct_light_intersect_result = false;
//                 ray.object_intersect_distance = std::numeric_limits<float>::max();
//                 ray.stage = models::ray_stage::INTERSECT;
//                 map_ray_stage_to_queue(ray);
//                 continue;
//             }

//             fvec3 normal = result.normal;
// 		    fvec3 outcoming = -ray.ray.get_dir();

//             if (math::dot(normal, outcoming) <= 0) {
//                 ray.stage = models::ray_stage::ACCUMULATE;
//                 map_ray_stage_to_queue(ray);
//                 continue;
//             }
			
//             roughness = math::max(roughness, 0.05F);
            
//             float specular_probability = pbr::fresnel(outcoming, reflect(-outcoming, normal), ior);
// 		    specular_probability = math::max(specular_probability, metallic);
// 		    bool specular_sample = core::rand() < specular_probability;

//             math::fvec3 direct_out;

//             if (m_scene.m_sun_light) {
//                 auto direct_incoming = ray.direct_light_ray.get_dir();

//                 if (math::dot(normal, direct_incoming) > 0) {
//                     if (!ray.direct_light_intersect_result) {
//                         if (result.shadow_catcher && ray.bounce == bounce_count) {
//                             geometry::ray opacity_ray(
//                                 result.position + ray.ray.get_dir() * math::epsilon,
//                                 ray.ray.get_dir()
//                             );
                            
//                             ray.ray = opacity_ray;
//                             ray.direct_light_intersect_result = false;
//                             ray.object_intersect_distance = std::numeric_limits<float>::max();
//                             ray.stage = models::ray_stage::INTERSECT;
//                             map_ray_stage_to_queue(ray);
//                             continue;
//                         }

//                         float diffuse_pdf = pbr::pdf_diffuse(normal, direct_incoming);
// 					    fvec3 diffuse_brdf = diffuse_pdf * albedo;      
                        
//                         float specular_pdf = pbr::pdf_specular(normal, outcoming, direct_incoming, roughness);
// 					    fvec3 specular_brdf(specular_pdf);

//                         fvec3 fresnel = lerp(fvec3(0.04F), albedo, metallic);
// 					    {
// 						    fvec3 halfway = normalize(outcoming + direct_incoming);
// 						    float cos_theta = dot(outcoming, halfway);

// 						    fresnel = lerp(fresnel, fvec3::one, math::pow(1 - cos_theta, 5));
// 					    }

//                         diffuse_brdf = lerp(diffuse_brdf, fvec3::zero, metallic);
//                         fvec3 brdf = lerp(diffuse_brdf, specular_brdf, fresnel);

//                         diffuse_pdf = 1;
//                         specular_pdf = 1;
//                         float pdf = lerp(diffuse_pdf, specular_pdf, specular_probability);
//                         fvec3 direct_in = m_scene.m_sun_light->get_component<scene::sun_light>()->energy;
//                         direct_out = brdf * direct_in / math::max(pdf, math::epsilon);
//                         direct_out = math::clamp(direct_out, fvec3::zero, direct_in);
//                     }
//                 }
//                 else {
//                     if (result.shadow_catcher && ray.bounce == bounce_count) {       
//                         ray.stage = models::ray_stage::ACCUMULATE;
//                         map_ray_stage_to_queue(ray);
//                         continue;
//                     }
//                 }
//             }

//             fvec3 indrect_out;
//             fvec2 rand(core::rand(), core::rand());
//             fvec3 indirect_incoming = specular_sample
// 			                          ? pbr::importance_specular(rand, normal, outcoming, roughness)
// 			                          : pbr::importance_diffuse(rand, normal, outcoming);

//             if (math::dot(normal, indirect_incoming) > 0) {
//                 float diffuse_pdf = pbr::pdf_diffuse(normal, indirect_incoming);
// 			    fvec3 diffuse_brdf = diffuse_pdf * albedo;
             
//                 float specular_pdf = pbr::pdf_specular(normal, outcoming, indirect_incoming, roughness);
// 			    fvec3 specular_brdf(specular_pdf);

//                 fvec3 fresnel = lerp(fvec3(0.04F), albedo, metallic);
// 			    {
// 				    fvec3 halfway = normalize(outcoming + indirect_incoming);
// 				    float cos_theta = dot(outcoming, halfway);

// 				    fresnel = lerp(fresnel, fvec3::one, math::pow(1 - cos_theta, 5));
// 			    }

//                 diffuse_brdf = lerp(diffuse_brdf, fvec3::zero, metallic);
//                 fvec3 brdf = lerp(diffuse_brdf, specular_brdf, fresnel);

//                 float pdf = lerp(diffuse_pdf, specular_pdf, specular_probability);
                
//                 geometry::ray indirect_ray(
//                     result.position + indirect_incoming * math::epsilon,
//                     indirect_incoming
//                 );

//                 fvec3 color = direct_out + emissive;
//                 color = color * ray.scale;

//                 ray.color = fvec4(fvec3(ray.color) + color, 1);
//                 fvec3 new_scale = ray.scale * (brdf / math::max(pdf, math::epsilon));
//                 ray.scale = math::clamp(new_scale, fvec3::zero, ray.scale);

//                 ray.bounce -= 1;
//                 ray.stage = ray.bounce > 0 ? models::ray_stage::INTERSECT : models::ray_stage::ACCUMULATE;
//                 ray.ray = indirect_ray;
//                 map_ray_stage_to_queue(ray);
//             }
//             else {
//                 fvec3 color = direct_out + emissive;
//                 color = color * ray.scale;

//                 ray.color = fvec4(fvec3(ray.color) + color, 1);
//                 ray.stage = models::ray_stage::ACCUMULATE;
//                 map_ray_stage_to_queue(ray);
//             }
//         }
//     }
// }
