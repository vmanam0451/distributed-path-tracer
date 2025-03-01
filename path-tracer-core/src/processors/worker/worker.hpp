#pragma once

#include "pch.hpp"
#include "processors/application.hpp"
#include "models/work_info.hpp"
#include "models/cloud_ray.hpp"
#include "cloud/s3.hpp"
#include "scene/scene.hpp"
#include <concurrentqueue/concurrentqueue.h>
#include <cstdint>
#include <sys/types.h>

namespace processors {
    class worker : public application {
    public:
        worker(const models::worker_info& worker_info);
        void run() override;
        ~worker() override;
    public:
        math::uvec2 resolution = math::fvec2(640, 480);
        uint32_t sample_count = 20;
		math::fvec3 environment_factor = math::fvec3::one;
		bool transparent_background = false;
        uint8_t bounce_count = 4;

    private:
        void download_gltf_file();

        void generate_rays();
        void map_ray_stage_to_queue(const models::cloud_ray& ray);

        void process_intersections();
        void process_intersection_results();

        void process_direct_lighting(); // handle opacity, and direct lighting
        void process_direct_lighting_results(); // process result from direct lighting and handle indirect lighting

        void process_indirect_lighting_results(); // process result from indirect lighting
        void process_completed_rays();
    
        std::vector<uint8_t> generate_final_image();
    private:
        struct pixel {
            math::fvec3 color;
            float alpha;
            bool claimed;
            uint32_t sample;
        };

    private:
        models::worker_info m_worker_info;
        std::filesystem::path m_gltf_file_path;
        cloud::distributed_scene m_scene;
        std::vector<std::vector<pixel>> pixels;

        std::atomic<bool> m_should_terminate;
        std::atomic<uint32_t> m_completed_rays;

        moodycamel::ConcurrentQueue<models::cloud_ray> m_intersection_queue;
        moodycamel::ConcurrentQueue<models::cloud_ray> m_intersection_result_queue;

        moodycamel::ConcurrentQueue<models::cloud_ray> m_direct_lighting_queue;
        moodycamel::ConcurrentQueue<models::cloud_ray> m_direct_lighting_result_queue;

        moodycamel::ConcurrentQueue<models::cloud_ray> m_indirect_lighting_queue;

        moodycamel::ConcurrentQueue<models::cloud_ray> m_completed_queue;

        std::map<std::string, std::vector<models::cloud_ray>> m_intersection_results;
    };
}