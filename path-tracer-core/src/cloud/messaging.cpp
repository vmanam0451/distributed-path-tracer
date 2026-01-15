#include "messaging.hpp"

using json = nlohmann::json;

namespace cloud
{
void sqs_poll(const models::SQSOptions &options, std::atomic<bool> &m_should_terminate,
              std::function<void(models::cloud_ray &ray)> callback)
{

    Aws::Client::ClientConfiguration config;
    config.requestTimeoutMs = (options.waitTimeSeconds + 10) * 1000; // Add 10 second buffer
    config.connectTimeoutMs = 5000;

    Aws::SQS::SQSClient sqs_client(config);

    spdlog::info("Starting SQS poll on queue: {}", options.queueUrl);

    while (!m_should_terminate)
    {
        Aws::SQS::Model::ReceiveMessageRequest receive_request;
        receive_request.SetQueueUrl(options.queueUrl);
        receive_request.SetMaxNumberOfMessages(options.maxMessages);
        receive_request.SetWaitTimeSeconds(options.waitTimeSeconds);
        receive_request.AddMessageAttributeNames("All");

        auto outcome = sqs_client.ReceiveMessage(receive_request);
        if (outcome.IsSuccess())
        {
            const auto &messages = outcome.GetResult().GetMessages();
            if (!messages.empty())
            {
                spdlog::info("Received {} messages from SQS", messages.size());
            }
            for (const auto &message : messages)
            {
                bool processed_successfully = false;
                try
                {
                    auto envelope = json::parse(message.GetBody());

                    std::string message_body = envelope["Message"].get<std::string>();

                    auto message_json = json::parse(message_body);
                    if (message_json.contains("rays"))
                    {
                        auto rays = message_json["rays"].get<std::vector<models::cloud_ray>>();
                        spdlog::info("Processing {} rays from SQS message", rays.size());
                        for (auto &ray : rays)
                        {
                            callback(ray);
                        }
                        spdlog::info("Finished processing {} rays from SQS message", rays.size());
                        processed_successfully = true;
                    }
                    else if (message_json.contains("terminate"))
                    {
                        m_should_terminate = true;
                        spdlog::info("Received termination signal via SQS.");
                        processed_successfully = true;
                    }
                    else
                    {
                        spdlog::warn("Received unknown message format via SQS: {}", 
                                    message_body.substr(0, std::min(message_body.size(), size_t(200))));
                    }
                }
                catch (const json::exception &e)
                {
                    spdlog::error("JSON parsing error: {}. First 300 chars: {}", 
                                 e.what(), message.GetBody().substr(0, std::min(message.GetBody().size(), size_t(300))));
                }

          
                Aws::SQS::Model::DeleteMessageRequest delete_req;
                delete_req.SetQueueUrl(options.queueUrl);
                delete_req.SetReceiptHandle(message.GetReceiptHandle());
                auto del_outcome = sqs_client.DeleteMessage(delete_req);
                if (!del_outcome.IsSuccess())
                {
                    spdlog::error("Failed to delete SQS message: {}", del_outcome.GetError().GetMessage());
                }
            }
        }
        else
        {
            spdlog::error("Failed to receive messages from SQS: {}", outcome.GetError().GetMessage());
        }

        std::this_thread::sleep_for(std::chrono::seconds(options.pollingIntervalSeconds));
    }
}

void sns_publish_message(Aws::SNS::SNSClient &sns_client, const std::string &topic_arn,
                         const std::string &source_worker_id, const std::string &target_id,
                         const std::vector<models::cloud_ray> &chunk)
{
    nlohmann::json rays_json;
    rays_json["rays"] = chunk;
    std::string message = rays_json.dump();

    Aws::SNS::Model::PublishRequest publish_request;
    publish_request.SetTopicArn(topic_arn);
    publish_request.SetMessage(message);

    Aws::SNS::Model::MessageAttributeValue worker_id_attr;
    worker_id_attr.SetDataType("String");
    worker_id_attr.SetStringValue(target_id);
    publish_request.AddMessageAttributes("worker_id", worker_id_attr);

    Aws::SNS::Model::MessageAttributeValue source_worker_id_attr;
    source_worker_id_attr.SetDataType("String");
    source_worker_id_attr.SetStringValue(source_worker_id);
    publish_request.AddMessageAttributes("source_worker_id", source_worker_id_attr);

    // Retry with exponential backoff until success
    constexpr int MAX_RETRIES = 10;
    constexpr int BASE_DELAY_MS = 100;

    for (int attempt = 0;; ++attempt)
    {
        auto outcome = sns_client.Publish(publish_request);

        if (outcome.IsSuccess())
        {
            spdlog::debug("Message published to SNS topic {} with worker ID {} ({} rays, {} bytes)", topic_arn,
                          target_id, chunk.size(), message.size());
            return;
        }

        if (attempt < MAX_RETRIES)
        {
            int delay_ms = BASE_DELAY_MS * (1 << attempt); // Exponential backoff
            spdlog::warn("Failed to publish message to SNS (attempt {}): {}. Retrying in {}ms...", attempt + 1,
                         outcome.GetError().GetMessage(), delay_ms);
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }
        else
        {
            // After max retries, keep trying with max delay
            spdlog::error("Failed to publish message to SNS (attempt {}): {}. Retrying in {}ms...", attempt + 1,
                          outcome.GetError().GetMessage(), BASE_DELAY_MS * (1 << MAX_RETRIES));
            std::this_thread::sleep_for(std::chrono::milliseconds(BASE_DELAY_MS * (1 << MAX_RETRIES)));
        }
    }
}

void sns_send_batch(const std::string &topic_arn, const std::string &source_worker_id, const std::string &target_id,
                    const std::vector<models::cloud_ray> &rays)
{
    if (rays.empty())
    {
        return;
    }

    Aws::SNS::SNSClient sns_client;

    std::vector<models::cloud_ray> current_batch;
    // Base overhead for {"rays":[]}
    constexpr size_t BASE_JSON_OVERHEAD = 12;
    size_t current_size = BASE_JSON_OVERHEAD;

    for (const auto &ray : rays)
    {
        nlohmann::json ray_json = ray;
        std::string ray_str = ray_json.dump();
        // +1 for comma separator between rays (except first ray)
        size_t ray_size_with_separator = ray_str.size() + (current_batch.empty() ? 0 : 1);

        // Check if adding this ray would exceed the limit
        if (current_size + ray_size_with_separator > models::SNS_USABLE_SIZE)
        {
            // Current batch is full, send it
            if (!current_batch.empty())
            {
                sns_publish_message(sns_client, topic_arn, source_worker_id, target_id, current_batch);
                current_batch.clear();
                current_size = BASE_JSON_OVERHEAD;
            }

            // Single ray exceeding ~255KB is impossible given cloud_ray structure (~500-800 bytes)
            assert(BASE_JSON_OVERHEAD + ray_str.size() <= models::SNS_USABLE_SIZE &&
                   "Single ray exceeds SNS message size limit - this should never happen");

            ray_size_with_separator = ray_str.size(); // No comma for first ray in new batch
        }

        current_batch.push_back(ray);
        current_size += ray_size_with_separator;
    }

    // Send remaining rays
    if (!current_batch.empty())
    {
        sns_publish_message(sns_client, topic_arn, source_worker_id, target_id, current_batch);
    }
}

void sns_signal_termination(const std::string &topic_arn, const std::string &worker_id)
{
    Aws::SNS::SNSClient sns_client;

    Aws::SNS::Model::PublishRequest publish_request;
    publish_request.SetTopicArn(topic_arn);

    Aws::SNS::Model::MessageAttributeValue worker_id_attr;
    worker_id_attr.SetDataType("String");
    worker_id_attr.SetStringValue(worker_id);

    publish_request.AddMessageAttributes("worker_id", worker_id_attr);

    Aws::SNS::Model::MessageAttributeValue source_worker_id_attr;
    source_worker_id_attr.SetDataType("String");
    source_worker_id_attr.SetStringValue("SYSTEM"); // Use a special ID that won't match any worker
    publish_request.AddMessageAttributes("source_worker_id", source_worker_id_attr);

    publish_request.SetMessage("{\"terminate\":true}");

    auto outcome = sns_client.Publish(publish_request);

    if (!outcome.IsSuccess())
    {
        spdlog::error("Failed to publish message to SNS: {}", outcome.GetError().GetMessage());
    }
    else
    {
        spdlog::info("Message published to SNS topic {} with worker ID {}", topic_arn, worker_id);
    }
}

} // namespace cloud