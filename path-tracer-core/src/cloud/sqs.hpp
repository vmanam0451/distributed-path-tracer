#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace cloud
{

// Content-batched SQS sender — packs thousands of pixel JSONs into single SQS
// message bodies (as JSON arrays), reducing ~3M SQS messages to ~1500.
class sqs_sender
{
  public:
    explicit sqs_sender(const std::string &queue_url);
    ~sqs_sender();

    // Enqueue a single pixel JSON string for batched sending
    void enqueue(const std::string &pixel_json);
    void send_terminate();
    void stop();

  private:
    void flush_loop();
    void send_message(const std::string &body);
    void flush_buffer(std::vector<std::string> &buffer);

    std::string m_queue_url;
    std::vector<std::string> m_buffer;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::thread m_flush_thread;
    std::atomic<bool> m_running{true};

    // ~85 bytes per pixel JSON → 2000 pixels ≈ 170KB, well under SQS 256KB limit
    static constexpr size_t PIXELS_PER_MESSAGE = 2000;
    static constexpr auto FLUSH_INTERVAL = std::chrono::milliseconds(200);
};

}