#include <cstdint>
#include <path_tracer/core/renderer.hpp>
#include <path_tracer/math/vec3.hpp>
#include <path_tracer/math/vec2.hpp>
#include <path_tracer/image/image.hpp>
#include <path_tracer/core/utils.hpp>
#include <path_tracer/util/thread_pool.hpp>
#include "path_tracer/util/rand_cone_vec.hpp"
#include <path_tracer/core/pbr.hpp>
#include "cloud/s3.hpp"
#include "models/cloud_ray.hpp"
#include "worker.hpp"


namespace processors {
    worker::worker(const models::worker_info& worker_info) {
        this->m_worker_info = worker_info;
        this->m_gltf_file_path = std::filesystem::path("/tmp/scene.gltf");
    }

    worker::~worker() {
        
    }

    void worker::run() {
        this->download_gltf_file();

        auto work = m_worker_info.scene_info.work;
        m_scene.load_scene(m_worker_info.scene_bucket, m_worker_info.scene_root, work, m_gltf_file_path);

        m_should_terminate = false;
        m_completed_rays = 0;

        generate_rays();

		pixels.resize(resolution.x);
		for (auto& column : pixels)
			column.resize(resolution.y, {math::fvec3::zero, 0, false, 0});

        std::vector<std::thread> shading_threads;
        for (int i = 0; i < 4; i++) {
            shading_threads.push_back(std::thread(&worker::process_shading, this));
        }
        
        std::thread accumulation_thread(&worker::process_accumulation, this);

        std::thread monitor_thread([&]() {
            uint32_t total_rays = resolution.x * resolution.y * sample_count;
            while (m_completed_rays < total_rays) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            m_should_terminate = true;
            spdlog::info("All rays processed, signaling termination");
        });

        std::thread debug_thread([&]() {
            while (!m_should_terminate) {
                spdlog::info("Queue sizes: INTERSECT={}, INTERSECT_RESULT={}, DIRECT={}, DIRECT_RESULT={}, INDIRECT={}, COMPLETED={}",
                    m_object_intersection_queue.size_approx(),
                    m_object_intersection_result_queue.size_approx(),
                    m_direct_lighting_intersection_queue.size_approx(),
                    m_direct_lighting_intersection_result_queue.size_approx(),
                    m_shading_queue.size_approx(),
                    m_accumulate_queue.size_approx());
                spdlog::info("Completed Rays: {}", m_completed_rays.load());
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        });
    
        debug_thread.join();

        accumulation_thread.join();
        monitor_thread.join();

        for (int i = 0; i < shading_threads.size(); i++) shading_threads[i].join();
         
        spdlog::info("Generating Image...");

	    auto png_data = generate_final_image();
        std::variant<std::filesystem::path, std::vector<uint8_t>> input{png_data};
        spdlog::info("Uploading image...");
        cloud::s3_upload_object(m_worker_info.scene_bucket, m_worker_info.scene_root + "test.png", input);
    }


    void worker::download_gltf_file() {
        std::string s3_gltf_file{m_worker_info.scene_root + "scene.gltf"};
        std::variant<std::filesystem::path, std::vector<uint8_t>> output { m_gltf_file_path };
        cloud::s3_download_object(m_worker_info.scene_bucket, s3_gltf_file, output);
    }

    void worker::generate_rays() {
        using namespace math;

        for(uint32_t x = 0; x < resolution.x; x++) {
            for(uint32_t y = 0; y < resolution.y; y++) {
                for(uint32_t sample = 0; sample < sample_count; sample++) {
                    std::string uuid = fmt::format("{}_{}_{}", x, y, sample);
                    
                    uvec2 pixel(x, y);

					fvec2 aa_offset;
                    if (sample == 0 && !transparent_background) {
                        aa_offset = fvec2(0, 0); 
                    } else {
                        aa_offset = fvec2(core::rand(), core::rand());
                    }

					fvec2 ndc = ((fvec2(pixel) + aa_offset) / resolution) * 2 - fvec2::one;
					ndc.y = -ndc.y;
					float ratio = static_cast<float>(resolution.x) / resolution.y;

					geometry::ray ray = m_scene.m_camera->get_component<scene::camera>()->get_ray(ndc, ratio);

                    models::cloud_ray cloud_ray;
                    cloud_ray.uuid = uuid;
                    cloud_ray.ray = ray;
                    cloud_ray.color = fvec4::zero;
                    cloud_ray.alpha = transparent_background ? 0.0f : 1.0f;
                    cloud_ray.scale = fvec3::one;
                    cloud_ray.bounce = bounce_count;
                    cloud_ray.stage = models::ray_stage::SHADING;

                    map_ray_stage_to_queue(cloud_ray);
                }
            } 
        }
    }

    void worker::map_ray_stage_to_queue(const models::cloud_ray& ray) {
        const models::ray_stage& stage = ray.stage;

        switch (stage) {
            case models::ray_stage::INTERSECT:
                m_object_intersection_queue.enqueue(ray);
                break;
            case models::ray_stage::DIRECT_LIGHTING:
                m_direct_lighting_intersection_queue.enqueue(ray);
                break;
            case models::ray_stage::SHADING:
                m_shading_queue.enqueue(ray);
                break;
            case models::ray_stage::ACCUMULATE:
                m_accumulate_queue.enqueue(ray);
                break;
            default:
                break;
        }
    }

    std::vector<uint8_t> worker::generate_final_image() {
        using namespace math;

        auto img = std::make_shared<image::image>(resolution, 4, false, true);

        for (uint32_t y = 0; y < resolution.y; y++) {
            for (uint32_t x = 0; x < resolution.x; x++) {
                fvec3 color = core::tonemap_approx_aces(pixels[x][y].color);
                float alpha = pixels[x][y].alpha;

                uvec2 pixel(x, y);
                img->write(pixel, 0, color.x);
                img->write(pixel, 1, color.y);
                img->write(pixel, 2, color.z);
                img->write(pixel, 3, alpha);
            }
        }

        return img->save_to_memory_png();
    }

    std::vector<uint8_t> worker::render() const {
        using namespace core;
        using namespace geometry;
        using namespace math;

		util::thread_pool pool(8);

		std::vector<std::shared_ptr<util::future>> todo;
		todo.reserve(resolution.y);

		// Claiming pixels by opaque samples is needed for proper transparent background blending
		
		std::vector<std::vector<pixel>> pixels;
		pixels.resize(resolution.x);
		for (auto& column : pixels)
			column.resize(resolution.y, {fvec3::zero, 0, false});

		auto img = std::make_shared<image::image>(resolution, 4, false, true);

		for (uint32_t sample = 0; sample < sample_count; sample++) {
			spdlog::info("Drawing sample {} out of {}", sample + 1, sample_count);

			for (uint32_t y = 0; y < resolution.y; y++) {
				todo.push_back(pool.submit([&, y](uint32_t) {
					for (uint32_t x = 0; x < resolution.x; x++) {
						uvec2 pixel(x, y);

						// Do not offset the first sample so we can get a consistent alpha mask for smart blending
						fvec2 aa_offset = fvec2(core::rand(), core::rand());

						fvec2 ndc = ((fvec2(pixel) + aa_offset) / resolution) * 2 - fvec2::one;
						ndc.y = -ndc.y;
						float ratio = static_cast<float>(resolution.x) / resolution.y;

						ray ray = m_scene.m_camera->get_component<scene::camera>()->get_ray(ndc, ratio);
						fvec4 data = trace_iter(bounce_count, ray);

						// Smart blending - needed for transparent background
						if (transparent_background) {
							if (data.w > 0.5 && !pixels[x][y].claimed) {
								// If an opaque sample will claim this pixel
								pixels[x][y].color = fvec3(data); // Overwrite the color
								pixels[x][y].alpha = 1 / (sample + 1); // And blend the alpha
								pixels[x][y].claimed = true; // Mark the pixel as claimed
								continue;
							}
							else if (data.w < 0.5 && pixels[x][y].claimed) {
								// If a transparent sample encounters a claimed pixel
								pixels[x][y].alpha = pixels[x][y].alpha * sample + data.w; // Blend only alpha
								pixels[x][y].alpha /= sample + 1;
								continue;
							}
							else if (data.w < 0.5) {
								// If transparent sample blends with an unclaimed pixel
								// Do nothing and preserve the default transparent black color
								continue;
							}
						}

						// Otherwise if an opaque sample blends with a claimed pixel (or transparent background is disabled)
						pixels[x][y].color = pixels[x][y].color * sample + fvec3(data); // Blend color
						pixels[x][y].color /= sample + 1;
						pixels[x][y].alpha = pixels[x][y].alpha * sample + data.w; // Blend alpha
						pixels[x][y].alpha /= sample + 1;
					}
				}));
			}

			for (auto& future : todo)
				future->wait();

			todo.clear();

			if (sample != 0 && sample % 5 == 0 || sample == sample_count - 1) {
				for (uint32_t y = 0; y < resolution.y; y++) {
					for (uint32_t x = 0; x < resolution.x; x++) {
						fvec3 color = tonemap_approx_aces(pixels[x][y].color);
						float alpha = pixels[x][y].alpha;

						uvec2 pixel(x, y);
						img->write(pixel, 0, color.x);
						img->write(pixel, 1, color.y);
						img->write(pixel, 2, color.z);
						img->write(pixel, 3, alpha);
					}
				}
			}
		}

		return img->save_to_memory_png();
	}

    fvec4 worker::trace_iter(uint8_t initial_bounce, const geometry::ray& initial_ray) const {
        using namespace core;
        using namespace math;
        using namespace scene;
        
        // Start with the initial ray
        geometry::ray current_ray = initial_ray;
        uint8_t bounce_remaining = initial_bounce;
        
        // Accumulated color and throughput (scaling factor)
        fvec3 accumulated_color = fvec3::zero;
        fvec3 throughput = fvec3::one;
        
        // Final alpha value for the pixel
        float alpha = transparent_background ? 0.0f : 1.0f;
        
        // Process the ray until it doesn't hit anything or runs out of bounces
        while (bounce_remaining > 0) {
            // Intersect with the scene
            auto result = m_scene.intersect(current_ray);
            
            // If no intersection, add environment contribution and exit
            if (!result.hit) {
                if (m_scene.m_environment) {
                    fvec3 env_color = fvec3(m_scene.m_environment->sample(
                        core::equirectangular_proj(current_ray.get_dir()))) * environment_factor;
                    accumulated_color += throughput * env_color;
                } else {
                    accumulated_color += throughput * environment_factor;
                }
                alpha = transparent_background ? 0.0f : 1.0f;
                break;
            }
            
            // We hit something, so alpha defaults to 1
            alpha = 1.0f;
            
            // Extract material properties
            fvec3 albedo = result.material->get_albedo(result.tex_coord);
            float opacity = result.material->get_opacity(result.tex_coord);
            float roughness = result.material->get_roughness(result.tex_coord);
            float metallic = result.material->get_metallic(result.tex_coord);
            fvec3 emissive = result.material->get_emissive(result.tex_coord) * 10;
            float ior = result.material->ior;
            
            // Add emissive contribution
            accumulated_color += throughput * emissive;
            
            // Handle opacity/transparency
            if (!math::is_approx(opacity, 1) && core::rand() > opacity) {
                // Continue ray through the surface
                current_ray = geometry::ray(
                    result.position + current_ray.get_dir() * math::epsilon,
                    current_ray.get_dir()
                );
                continue; // Skip the rest of this loop iteration
            }
            
            // Calculate surface normals and ray directions
            fvec3 normal = result.get_normal();
            fvec3 outcoming = -current_ray.get_dir();
            
            // Check if the ray is coming from behind the surface
            if (math::dot(normal, outcoming) <= 0) {
                break; // Exit the loop as we can't properly shade this hit
            }
            
            // Shadow catcher logic for the first bounce
            if (result.material->shadow_catcher && bounce_remaining == initial_bounce) {
                // For shadow catchers, we need to check if there's direct light or not
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
                    // In shadow - return black with alpha 0
                    return fvec4::future;
                } else {
                    // Not in shadow - continue the ray as if transparent
                    current_ray = geometry::ray(
                        result.position + current_ray.get_dir() * math::epsilon,
                        current_ray.get_dir()
                    );
                    continue;
                }
            }
            
            // Correct roughness to avoid precision issues
            roughness = math::max(roughness, 0.05F);
            
            // Calculate specular probability
            float specular_probability = core::pbr::fresnel(outcoming, reflect(-outcoming, normal), ior);
            specular_probability = math::max(specular_probability, metallic);
            bool specular_sample = core::rand() < specular_probability;
            
            // Handle direct lighting
            if (m_scene.m_sun_light) {
                fvec3 direct_incoming = m_scene.m_sun_light->get_global_transform().basis * fvec3::backward;
                direct_incoming = util::rand_cone_vec(
                    core::rand(), 
                    math::cos(core::rand() * m_scene.m_sun_light->get_component<scene::sun_light>()->angular_radius),
                    direct_incoming
                );
                
                // Only compute direct lighting if the light is above the surface
                if (math::dot(normal, direct_incoming) > 0) {
                    geometry::ray direct_ray(
                        result.position + direct_incoming * math::epsilon,
                        direct_incoming
                    );
                    auto direct_result = m_scene.intersect(direct_ray);
                    
                    if (!direct_result.hit) {
                        // Calculate diffuse BRDF
                        float diffuse_pdf = pbr::pdf_diffuse(normal, direct_incoming);
                        fvec3 diffuse_brdf = diffuse_pdf * albedo;
                        
                        // Calculate specular BRDF
                        float specular_pdf = pbr::pdf_specular(normal, outcoming, direct_incoming, roughness);
                        fvec3 specular_brdf(specular_pdf);
                        
                        // Calculate fresnel
                        fvec3 fresnel = lerp(fvec3(0.04F), albedo, metallic);
                        {
                            fvec3 halfway = normalize(outcoming + direct_incoming);
                            float cos_theta = dot(outcoming, halfway);
                            fresnel = lerp(fresnel, fvec3::one, math::pow(1 - cos_theta, 5));
                        }
                        
                        // Combine BRDFs
                        diffuse_brdf = lerp(diffuse_brdf, fvec3::zero, metallic);
                        fvec3 brdf = lerp(diffuse_brdf, specular_brdf, fresnel);
                        
                        // Simplified direct light PDF since we're directly sampling the light
                        diffuse_pdf = 1;
                        specular_pdf = 1;
                        float pdf = lerp(diffuse_pdf, specular_pdf, specular_probability);
                        
                        // Get light energy and calculate direct light contribution
                        fvec3 direct_in = m_scene.m_sun_light->get_component<scene::sun_light>()->energy;
                        fvec3 direct_out = brdf * direct_in / math::max(pdf, math::epsilon);
                        direct_out = math::clamp(direct_out, fvec3::zero, direct_in);
                        
                        // Add direct lighting contribution
                        accumulated_color += throughput * direct_out;
                    }
                }
            }
            
            // Sample direction for indirect lighting based on material properties
            fvec2 rand_val(core::rand(), core::rand());
            fvec3 indirect_incoming = specular_sample
                ? pbr::importance_specular(rand_val, normal, outcoming, roughness)
                : pbr::importance_diffuse(rand_val, normal, outcoming);
            
            // Only continue tracing if the new direction is above the surface
            if (math::dot(normal, indirect_incoming) > 0) {
                // Calculate diffuse BRDF
                float diffuse_pdf = pbr::pdf_diffuse(normal, indirect_incoming);
                fvec3 diffuse_brdf = diffuse_pdf * albedo;
                
                // Calculate specular BRDF
                float specular_pdf = pbr::pdf_specular(normal, outcoming, indirect_incoming, roughness);
                fvec3 specular_brdf(specular_pdf);
                
                // Calculate fresnel
                fvec3 fresnel = lerp(fvec3(0.04F), albedo, metallic);
                {
                    fvec3 halfway = normalize(outcoming + indirect_incoming);
                    float cos_theta = dot(outcoming, halfway);
                    fresnel = lerp(fresnel, fvec3::one, math::pow(1 - cos_theta, 5));
                }
                
                // Combine BRDFs
                diffuse_brdf = lerp(diffuse_brdf, fvec3::zero, metallic);
                fvec3 brdf = lerp(diffuse_brdf, specular_brdf, fresnel);
                
                // Calculate PDF
                float pdf = lerp(diffuse_pdf, specular_pdf, specular_probability);
                
                // Update throughput for next bounce
                throughput *= brdf / math::max(pdf, math::epsilon);
                
                // Clamp throughput to prevent fireflies
                throughput = math::clamp(throughput, fvec3::zero, fvec3(10.0f));
                
                // Update ray for next bounce
                current_ray = geometry::ray(
                    result.position + indirect_incoming * math::epsilon,
                    indirect_incoming
                );
                
                // Russian Roulette termination
                if (bounce_remaining < initial_bounce - 2) {
                    float p = math::max(throughput.x, math::max(throughput.y, throughput.z));
                    if (core::rand() > p) {
                        break;  // Terminate the path
                    }
                    throughput /= p; // Compensate for termination
                }
            } else {
                // No valid indirect direction, terminate path
                break;
            }
            
            // Use the next bounce
            bounce_remaining--;
        }
        
        return fvec4(accumulated_color, alpha);
    }

}