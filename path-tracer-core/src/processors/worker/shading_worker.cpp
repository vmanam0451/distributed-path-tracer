#include "models/cloud_ray.hpp"
#include "worker.hpp"
#include <path_tracer/core/pbr.hpp>
#include <path_tracer/core/utils.hpp>
#include <path_tracer/math/vec3.hpp>
#include <path_tracer/util/rand_cone_vec.hpp>

namespace processors {

    void worker::process_shading() {
        using namespace math;
        using namespace core;

        while (!m_should_terminate) {
            models::cloud_ray ray{};
            if (!m_shading_queue.try_dequeue(ray)) {
                std::this_thread::yield();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            
            geometry::ray current_ray = ray.ray;
            uint8_t bounce_remaining = ray.bounce;

            fvec4 color = trace_iter(bounce_remaining, current_ray);

            ray.color = fvec3(color);
            ray.alpha = color.w;
            ray.stage = models::ray_stage::ACCUMULATE;
            map_ray_stage_to_queue(ray);
        } 
    }
} // namespace processors
