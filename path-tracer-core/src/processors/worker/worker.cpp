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

        renderer.entities = m_scene.entities;
        renderer.camera = m_scene.camera;
        renderer.sun_light = m_scene.sun_light;
        renderer.environment = m_scene.environment;

	    renderer.sample_count = 25;
	    renderer.bounce_count = 4;
	    renderer.resolution = math::uvec2(1920, 1080);
	    renderer.environment_factor = math::fvec3::zero;
	    renderer.transparent_background = true;
	    
	    renderer.render("renders/test.png");
    }

    void worker::download_gltf_file() {
        std::string s3_gltf_file{m_worker_info.scene_root + "scene.gltf"};
        std::variant<std::filesystem::path, std::vector<uint8_t>> output { m_gltf_file_path };
        cloud::s3_download_object(m_worker_info.scene_bucket, s3_gltf_file, output);
    }
}