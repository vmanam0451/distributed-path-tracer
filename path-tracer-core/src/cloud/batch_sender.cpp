#include "batch_sender.hpp"

void batch_sender::enqueue_ray(const models::cloud_ray &ray, const std::string &target_id)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_pending_rays[target_id].push_back(ray);

    if (m_pending_rays[target_id].size() >= m_batch_size)
    {
        auto rays_to_send = std::move(m_pending_rays[target_id]);
        m_pending_rays[target_id].clear();
        lock.unlock();
        send_batch(target_id, rays_to_send);
    }
}

void batch_sender::flush_loop() {
    while (true)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait_for(lock, m_flush_interval);
        flush_all();
    }
}

void batch_sender::flush_all() {
    std::unique_lock<std::mutex> lock(m_mutex);
    for (auto &pair : m_pending_rays)
    {
        const std::string &target_id = pair.first;
        auto &rays = pair.second;

        if (!rays.empty())
        {
            auto rays_to_send = std::move(rays);
            rays.clear();
            lock.unlock();
            send_batch(target_id, rays_to_send);
            lock.lock();
        }
    }
}
