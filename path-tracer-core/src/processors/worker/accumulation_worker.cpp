#include "worker.hpp"
#include <cstdint>

namespace processors {
    void worker::process_accumulation() {
        using namespace core;
        using namespace math;

        while (!m_should_terminate) {
            models::cloud_ray ray{};
            if(!m_accumulate_queue.try_dequeue(ray)) {
                std::this_thread::yield();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            m_completed_rays++;

            uint32_t x = (ray.uuid >> 40) & 0xFFFFF;  
            uint32_t y = (ray.uuid >> 20) & 0xFFFFF;

            fvec4 data = fvec4(ray.color, ray.alpha);
        
            uint32_t sample = pixels[x][y].sample;

            if (transparent_background) {
                if (data.w > 0.5 && !pixels[x][y].claimed) {
                  pixels[x][y].color = fvec3(data);
                  pixels[x][y].alpha = 1 / (sample + 1);
                  pixels[x][y].claimed = true;
                  pixels[x][y].sample = sample + 1;
                  continue;
                } 
                else if(data.w < 0.5 && pixels[x][y].claimed) {
                  pixels[x][y].alpha = pixels[x][y].alpha * sample + data.w;
                  pixels[x][y].alpha /= sample + 1;
                  pixels[x][y].sample = sample + 1;
                  continue;
                } 
                else if(data.w < 0.5) {
                  pixels[x][y].sample = sample + 1;
                  continue;
                }
            }

            pixels[x][y].color = pixels[x][y].color * sample + fvec3(data);
            pixels[x][y].color /= sample + 1;

            pixels[x][y].alpha = pixels[x][y].alpha * sample + data.w; 
            pixels[x][y].alpha /= sample + 1;

            pixels[x][y].sample = sample + 1;
        }
    }
}