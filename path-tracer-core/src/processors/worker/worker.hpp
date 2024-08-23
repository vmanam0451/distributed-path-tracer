#pragma once

#include <processors/application.hpp>
#include <models/work_info.hpp>
#include <cloud/s3.hpp>

namespace processors {
    class worker : public application {
    public:
        worker(const models::work_info& work_info);
        void run() override;
        ~worker() override;
    private:
        void download_gltf_file();

    private:
        models::work_info m_work_info;
        std::filesystem::path m_gltf_file_path;
    };
}