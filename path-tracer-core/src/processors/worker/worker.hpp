#pragma once

#include "processors/application.hpp"
#include "models/work_info.hpp"
#include "cloud/s3.hpp"
#include "scene/scene.hpp"

namespace processors {
    class worker : public application {
    public:
        worker(const models::worker_info& worker_info);
        void run() override;
        ~worker() override;
    private:
        void download_gltf_file();

    private:
        models::worker_info m_worker_info;
        std::filesystem::path m_gltf_file_path;
        cloud::distributed_scene m_scene;

    };
}