#include <cstdint>
#include <path_tracer/core/renderer.hpp>
#include <path_tracer/math/vec3.hpp>
#include <path_tracer/math/vec2.hpp>
#include <path_tracer/image/image.hpp>
#include <path_tracer/core/utils.hpp>
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

        std::thread intersection_thread(&worker::process_intersections, this);
        std::thread intersection_result_thread(&worker::process_intersection_results, this);
        std::thread direct_lighting_thread(&worker::process_direct_lighting, this);
        std::thread direct_lighting_result_thread(&worker::process_direct_lighting_results, this);
        std::thread indirect_lighting_thread(&worker::process_indirect_lighting_results, this);
        std::thread completed_rays_thread(&worker::process_completed_rays, this);

        std::thread monitor_thread([&]() {
            uint32_t total_rays = resolution.x * resolution.y * sample_count;
            while (m_completed_rays < total_rays) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            m_should_terminate = true;
            spdlog::info("All rays processed, signaling termination");
        });

        intersection_thread.join();
        intersection_result_thread.join();
        direct_lighting_thread.join();
        direct_lighting_result_thread.join();
        indirect_lighting_thread.join();
        completed_rays_thread.join();
        monitor_thread.join();

         
	    auto png_data = generate_final_image();
        std::variant<std::filesystem::path, std::vector<uint8_t>> input{png_data};
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

					fvec2 aa_offset = fvec2(rand(), rand());

					fvec2 ndc = ((fvec2(pixel) + aa_offset) / resolution) * 2 - fvec2::one;
					ndc.y = -ndc.y;
					float ratio = static_cast<float>(resolution.x) / resolution.y;

					geometry::ray ray = m_scene.m_camera->get_component<scene::camera>()->get_ray(ndc, ratio);

                    models::cloud_ray cloud_ray;
                    cloud_ray.uuid = uuid;
                    cloud_ray.geometry_ray = ray;
                    cloud_ray.intersect_ray = ray;
                    cloud_ray.color = fvec4::zero;
                    cloud_ray.scale = fvec3::one;
                    cloud_ray.bounce = bounce_count;
                    cloud_ray.stage = models::ray_stage::INITIAL;

                    map_ray_stage_to_queue(cloud_ray);
                }
            } 
        }
    }

    void worker::map_ray_stage_to_queue(const models::cloud_ray& ray) {
        const models::ray_stage& stage = ray.stage;

        switch (stage) {
            case models::ray_stage::INITIAL:
                m_intersection_queue.enqueue(ray);
                break;
            case models::ray_stage::DIRECT_LIGHTING:
                m_direct_lighting_queue.enqueue(ray);
                break;
            case models::ray_stage::DIRECT_LIGHTING_RESULTS:
                m_direct_lighting_result_queue.enqueue(ray);
                break;
            case models::ray_stage::INDIRECT_LIGHTING:
                m_indirect_lighting_queue.enqueue(ray);
                break;
            case models::ray_stage::COMPLETED:
                m_completed_queue.enqueue(ray);
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

}