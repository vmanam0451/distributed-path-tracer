#include <path_tracer/core/renderer.hpp>
#include <path_tracer/math/vec3.hpp>
#include <path_tracer/math/vec2.hpp>
#include "cloud/s3.hpp"
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

        std::thread intersection_thread(&worker::process_intersections, this);
        std::thread intersection_result_thread(&worker::process_intersection_results, this);
        std::thread direct_lighting_thread(&worker::process_direct_lighting, this);
        std::thread direct_lighting_result_thread(&worker::process_direct_lighting_results, this);
        std::thread indirect_lighting_thread(&worker::process_indirect_lighting_results, this);
        std::thread completed_rays_thread(&worker::process_completed_rays, this);



        intersection_thread.join();
        intersection_result_thread.join();
        direct_lighting_thread.join();
        direct_lighting_result_thread.join();
        indirect_lighting_thread.join();
        completed_rays_thread.join();

         
	    auto png_data = generate_final_image();
        std::variant<std::filesystem::path, std::vector<uint8_t>> input{png_data};
        cloud::s3_upload_object(m_worker_info.scene_bucket, m_worker_info.scene_root + "test.png", input);
    }

    void worker::download_gltf_file() {
        std::string s3_gltf_file{m_worker_info.scene_root + "scene.gltf"};
        std::variant<std::filesystem::path, std::vector<uint8_t>> output { m_gltf_file_path };
        cloud::s3_download_object(m_worker_info.scene_bucket, s3_gltf_file, output);
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

}