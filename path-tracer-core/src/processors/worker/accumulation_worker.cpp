// #include "worker.hpp"
// #include <cstdint>

// namespace processors {
//     void worker::process_accumulation() {
//         using namespace core;
//         using namespace math;

//         while (!m_should_terminate) {
//             models::cloud_ray ray{};
//             if(!m_accumulate_queue.try_dequeue(ray)) {
//                 std::this_thread::yield();
//                 std::this_thread::sleep_for(std::chrono::milliseconds(1));
//                 continue;
//             }

//             m_completed_rays++;

//             std::vector<std::string> parts;
//             std::stringstream ss(ray.uuid);
//             std::string part;

//             while (std::getline(ss, part, '_')) {
//                 parts.push_back(part);
//             }

//             uint32_t x = std::stoi(parts[0]);
//             uint32_t y = std::stoi(parts[1]);

//             fvec4 data = ray.color;
        
//             uint32_t sample = pixels[x][y].sample;

//             if (transparent_background) {
//                 if (data.w > 0.5 && !pixels[x][y].claimed) {
//                   pixels[x][y].color = fvec3(data);
//                   pixels[x][y].alpha = 1 / (sample + 1);
//                   pixels[x][y].claimed = true;
//                   pixels[x][y].sample = sample + 1;
//                   continue;
//                 } 
//                 else if(data.w < 0.5 && pixels[x][y].claimed) {
//                   pixels[x][y].alpha = pixels[x][y].alpha * sample + data.w;
//                   pixels[x][y].alpha /= sample + 1;
//                   pixels[x][y].sample = sample + 1;
//                   continue;
//                 } 
//                 else if(data.w < 0.5) {
//                   continue;
//                 }
//             }

//             pixels[x][y].color = pixels[x][y].color * sample + fvec3(data);
//             pixels[x][y].color /= sample + 1;
//             pixels[x][y].alpha = pixels[x][y].alpha * sample + data.w; 
//             pixels[x][y].alpha /= sample + 1;
//             pixels[x][y].sample = sample + 1;
//         }
//     }
// }