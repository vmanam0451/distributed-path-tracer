#pragma once

#include "models/cloud_ray.hpp"
#include "models/work_info.hpp"
#include "path_tracer/math/vec3.hpp"
#include "pch.hpp"
#include "processors/application.hpp"

namespace processors
{
class master : public application
{
  public:
    master(const models::worker_info &worker_info);
    void run() override;
    ~master() override;

  private:
    void process_accumulation();
    std::vector<uint8_t> generate_final_image();
    void process_ray_from_queue(const models::cloud_ray &ray);

  private:
    struct pixel
    {
        math::fvec3 color;
        float alpha;
        bool claimed;
        uint32_t sample;
    };

  private:
    models::worker_info m_worker_info;
    std::vector<std::vector<pixel>> pixels;
    math::uvec2 resolution = math::uvec2(640, 480);
    bool transparent_background = false;
    uint32_t sample_count = 50;

    std::atomic<uint32_t> m_completed_rays;
    std::atomic<bool> m_should_terminate;

    moodycamel::ConcurrentQueue<models::cloud_ray> m_accumulate_queue;
};
} // namespace processors