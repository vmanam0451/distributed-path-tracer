#include "worker.hpp"

namespace processors {
    void worker::process_completed_rays() {
        using namespace core;
        using namespace math;

        while (!m_should_terminate) {
            models::cloud_ray ray{};
            if(!m_completed_queue.try_dequeue(ray)) {
                std::this_thread::yield();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

        }
    }
}