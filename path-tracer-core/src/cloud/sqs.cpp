#include "sqs.hpp"
#include <pch.hpp>

namespace cloud
{

// ── Content-batched SQS sender ────────────────────────────────────────────────

sqs_sender::sqs_sender(const std::string &queue_url) : m_queue_url(queue_url)
{
    m_flush_thread = std::thread(&sqs_sender::flush_loop, this);
}

sqs_sender::~sqs_sender()
{
    stop();
}

void sqs_sender::enqueue(const std::string &pixel_json)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_buffer.push_back(pixel_json);
    }
    if (m_buffer.size() >= PIXELS_PER_MESSAGE)
    {
        m_cv.notify_one();
    }
}

void sqs_sender::send_terminate()
{
    // Stop the flush thread and drain remaining pixels
    stop();

    // Send terminate message
    static Aws::SQS::SQSClient sqs_client;
    Aws::SQS::Model::SendMessageRequest req;
    req.SetQueueUrl(m_queue_url.c_str());
    req.SetMessageBody("terminate");
    req.SetMessageAttributes(
        {{"Terminate", Aws::SQS::Model::MessageAttributeValue().WithDataType("String").WithStringValue("true")}});

    auto outcome = sqs_client.SendMessage(req);
    if (!outcome.IsSuccess())
    {
        auto error = outcome.GetError();
        spdlog::error("SQS terminate send error: {}: {}", error.GetExceptionName(), error.GetMessage());
    }
}

void sqs_sender::stop()
{
    if (!m_running.exchange(false))
        return;

    m_cv.notify_one();
    if (m_flush_thread.joinable())
    {
        m_flush_thread.join();
    }

    // Final drain
    std::lock_guard<std::mutex> lock(m_mutex);
    flush_buffer(m_buffer);
}

void sqs_sender::flush_loop()
{
    while (m_running)
    {
        std::vector<std::string> buffer;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait_for(lock, FLUSH_INTERVAL, [this] {
                return m_buffer.size() >= PIXELS_PER_MESSAGE || !m_running;
            });
            std::swap(buffer, m_buffer);
        }
        flush_buffer(buffer);
    }
}

void sqs_sender::flush_buffer(std::vector<std::string> &buffer)
{
    // Pack up to PIXELS_PER_MESSAGE pixel JSONs into each SQS message body as a JSON array
    for (size_t i = 0; i < buffer.size(); i += PIXELS_PER_MESSAGE)
    {
        size_t end = std::min(i + PIXELS_PER_MESSAGE, buffer.size());

        std::string body = "[";
        for (size_t j = i; j < end; ++j)
        {
            if (j > i)
                body += ",";
            body += buffer[j];
        }
        body += "]";

        send_message(body);
    }
    buffer.clear();
}

void sqs_sender::send_message(const std::string &body)
{
    static Aws::SQS::SQSClient sqs_client;

    Aws::SQS::Model::SendMessageRequest req;
    req.SetQueueUrl(m_queue_url.c_str());
    req.SetMessageBody(body.c_str());

    auto outcome = sqs_client.SendMessage(req);
    if (!outcome.IsSuccess())
    {
        auto error = outcome.GetError();
        spdlog::error("SQS send error: {}: {}", error.GetExceptionName(), error.GetMessage());
    }
}

} // namespace cloud

} // namespace cloud