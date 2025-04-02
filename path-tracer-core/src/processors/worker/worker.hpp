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
        uint32_t sample_count = 50;
		math::fvec3 environment_factor = math::fvec3::one;
		bool transparent_background = false;
        uint8_t bounce_count = 10;

    private:
        void download_gltf_file();

        void generate_rays();
        void map_ray_stage_to_queue(const models::cloud_ray& ray);

        void process_object_intersections();
        void process_object_intersection_results();



        void process_direct_lighting_intersections();
        void process_direct_lighting_intersection_results(); 

        void process_shading();
        void process_accumulation();

        std::vector<uint8_t> render() const;
        math::fvec4 trace_iter(uint8_t initial_bounce, const geometry::ray& initial_ray) const;

        // TODO: 
        /*
            Simplify lifecycle.
            Should Only need 3.
                - Intersection w/ object
                - Intersection w/ direct light
                - Accumulate
            Test with this simplified lifecycle.
            If not working, test with keeping track of samples and accumulating them in the complete stage
                i.e ray has vector<samples> sample={direct light, emissive, scale}
            If not working, test intersection with renderer. Call scene.intersect inside renderer and make sure that is working
        */
    
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

        moodycamel::ConcurrentQueue<models::cloud_ray> m_object_intersection_queue;
        moodycamel::ConcurrentQueue<models::cloud_ray> m_object_intersection_result_queue;

        moodycamel::ConcurrentQueue<models::cloud_ray> m_direct_lighting_intersection_queue;
        moodycamel::ConcurrentQueue<models::cloud_ray> m_direct_lighting_intersection_result_queue;

        moodycamel::ConcurrentQueue<models::cloud_ray> m_shading_queue;

        moodycamel::ConcurrentQueue<models::cloud_ray> m_accumulate_queue;

        std::map<uint64_t, std::pair<int, models::cloud_ray>> m_object_intersection_results;
        std::map<uint64_t, std::pair<int, models::cloud_ray>> m_direct_lighting_intersection_results;
    };
}