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

        core::renderer renderer;

        renderer.entities = std::move(m_scene.m_entities);
        renderer.camera = std::move(m_scene.m_camera);
        renderer.sun_light = std::move(m_scene.m_sun_light);
        renderer.environment = std::move(m_scene.m_environment);

	    renderer.sample_count = 2;
	    renderer.bounce_count = 4;
	    renderer.resolution = math::uvec2(1920, 1080);
	    renderer.environment_factor = math::fvec3::zero;
	    renderer.transparent_background = true;
	    
	    auto png_data = renderer.render();
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
                break;
            default:
                break;
        }
    }

}