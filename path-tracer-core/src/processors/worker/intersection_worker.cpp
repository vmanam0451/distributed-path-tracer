#include "models/intersect_result.hpp"
#include "worker.hpp"
#include <thread>

namespace processors {
    void worker::process_intersections() {
        while (!m_should_terminate) {
            models::cloud_ray ray{};
            if(!m_intersection_queue.try_dequeue(ray)) {
                std::this_thread::yield();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            auto result = m_scene.intersect(ray);
        }
    }

    void worker::process_intersection_results() {
        while (!m_should_terminate) {
            models::intersect_result result{};
            if(!m_intersection_result_queue.try_dequeue(result)) {
                std::this_thread::yield();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            
        }
    }
}