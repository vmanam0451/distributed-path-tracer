#pragma once

#include "pch.hpp"
#include "processors/application.hpp"
#include "models/work_info.hpp"
#include "models/cloud_ray.hpp"
#include "cloud/s3.hpp"
#include "scene/scene.hpp"
#include <concurrentqueue/concurrentqueue.h>

namespace processors {
    class worker : public application {
    public:
        worker(const models::worker_info& worker_info);
        void run() override;
        ~worker() override;
    private:
        void download_gltf_file();
        void process_intersections();
        void process_intersection_results();

    private:
        models::worker_info m_worker_info;
        std::filesystem::path m_gltf_file_path;
        cloud::distributed_scene m_scene;

        std::atomic<bool> m_should_terminate;

        moodycamel::ConcurrentQueue<models::cloud_ray> m_intersection_queue;
        moodycamel::ConcurrentQueue<models::intersect_result> m_intersection_result_queue;
    };
}