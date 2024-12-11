#include "worker.hpp"
#include <thread>

namespace processors {
    void worker::process_intersections() {
        while (!m_should_terminate) {
            cloud::ray ray{};
            if(!intersection_queue.try_dequeue(ray)) {
                std::this_thread::yield();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            auto result = m_scene.intersect(ray.geometry_ray);
        }
    }
}