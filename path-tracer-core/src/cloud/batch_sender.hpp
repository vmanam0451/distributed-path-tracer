
#include "models/cloud_ray.hpp"
#include <chrono>
#include <condition_variable>
#include <string>

namespace cloud
{

class batch_sender
{
  public:
    batch_sender(const std::string &topic_arn, const std::string &source_worker_id, size_t batch_size = 1000,
                 std::chrono::milliseconds flush_interval = std::chrono::milliseconds(50))
        : m_topic_arn(topic_arn), m_source_worker_id(source_worker_id), m_batch_size(batch_size),
          m_flush_interval(flush_interval)
    {
    }

    ~batch_sender()
    {
        m_terminate = true;
        m_cv.notify_all();
    }

    void enqueue_ray(const models::cloud_ray &ray, const std::string &target_id);
    void flush_loop();

  private:
    void send_batch(const std::string &target_id, const std::vector<models::cloud_ray> &rays);

  private:
    std::string m_topic_arn;
    std::string m_source_worker_id;
    size_t m_batch_size;
    std::chrono::milliseconds m_flush_interval;

    std::unordered_map<std::string, std::vector<models::cloud_ray>> m_pending_rays;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::atomic<bool> m_terminate = false;
};
} // namespace cloud