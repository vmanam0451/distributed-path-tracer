#include "batch_sender.hpp"
#include "messaging.hpp"

namespace cloud
{
void batch_sender::enqueue_ray(const models::cloud_ray &ray, const std::string &target_id)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_pending_rays[target_id].push_back(ray);

    if (m_pending_rays[target_id].size() >= m_batch_size)
    {
        auto rays_to_send = std::move(m_pending_rays[target_id]);
        m_pending_rays[target_id].clear();
        m_pending_rays[target_id].reserve(m_batch_size);
        lock.unlock();
        send_batch(target_id, rays_to_send);
    }
}

void batch_sender::flush_loop()
{
    while (!m_terminate)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait_for(lock, m_flush_interval, [this]() { return m_terminate.load(); });

        if (m_terminate)
        {
            break;
        }

        std::vector<std::pair<std::string, std::vector<models::cloud_ray>>> batches_to_send;
        for (auto &pair : m_pending_rays)
        {
            const std::string &target_id = pair.first;
            auto &rays = pair.second;

            if (!rays.empty())
            {
                batches_to_send.emplace_back(target_id, std::move(rays));
                rays.clear();
            }
        }

        lock.unlock();

        for (auto &batch : batches_to_send)
        {
            send_batch(batch.first, batch.second);
        }
    }
}

void batch_sender::send_batch(const std::string &target_id, const std::vector<models::cloud_ray> &rays)
{
    if (rays.empty())
    {
        return;
    }

    cloud::sns_send_batch(m_topic_arn, m_source_worker_id, target_id, rays);
}

void batch_sender::stop()
{
    m_terminate = true;
    m_cv.notify_all();
}
} // namespace cloud
