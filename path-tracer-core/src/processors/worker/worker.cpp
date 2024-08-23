#include "worker.hpp"
#include <cloud/s3.hpp>

namespace processors {
    worker::worker(const models::work_info& work_info) {
        this->m_work_info = work_info;
        this->m_gltf_file_path = std::filesystem::path("/tmp/scene.gltf");
    }

    worker::~worker() {
        
    }

    void worker::run() {
        this->download_gltf_file();
    }

    void worker::download_gltf_file() {
        std::string s3_gltf_file = this->m_work_info.scene_root + "/scene.gltf";
        std::variant<std::filesystem::path, std::vector<uint8_t>> output { this->m_gltf_file_path };
        cloud::s3_download_object(this->m_work_info.scene_bucket, s3_gltf_file, output);
    }
}