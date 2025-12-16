#include "messaging.hpp"

using json = nlohmann::json;

namespace cloud
{
void sqs_poll(const models::SQSOptions &options, std::atomic<bool> &m_should_terminate,
              std::function<void(models::cloud_ray &ray)> callback)
{
    Aws::SQS::SQSClient sqs_client;

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
            for (const auto &message : messages)
            {
                try
                {
                    auto envelope = json::parse(message.GetBody());

                    std::string message_body = envelope["Message"].get<std::string>();

                    auto message_json = json::parse(message_body);
                    if (message_json.contains("rays"))
                    {
                        auto rays = message_json["rays"].get<std::vector<models::cloud_ray>>();
                        for (auto &ray : rays)
                        {
                            callback(ray);
                        }
                    }
                    else if (message_json.contains("terminate"))
                    {
                        m_should_terminate = true;
                        spdlog::info("Received termination signal via SQS.");
                    }
                    else
                    {
                        spdlog::warn("Received unknown message format via SQS: {}", message_body);
                    }

                    Aws::SQS::Model::DeleteMessageRequest delete_req;
                    delete_req.SetQueueUrl(options.queueUrl);
                    delete_req.SetReceiptHandle(message.GetReceiptHandle());
                    sqs_client.DeleteMessage(delete_req);
                }
                catch (const json::exception &e)
                {
                    spdlog::error("JSON parsing error: {}", e.what());
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

void sns_send_batch(const std::string &topic_arn, const std::string &source_worker_id, const std::string &target_id,
                    const std::vector<models::cloud_ray> &rays)
{
    Aws::SNS::SNSClient sns_client;

    nlohmann::json sample_ray_json = rays[0];
    size_t ray_size = sample_ray_json.dump().size();
    ray_size = static_cast<size_t>(ray_size * 1.2); // Add some buffer for JSON overhead

    size_t max_rays_per_message = models::SNS_USABLE_SIZE / ray_size;
    max_rays_per_message = std::max(max_rays_per_message, static_cast<size_t>(1));

    for (size_t i = 0; i < rays.size(); i += max_rays_per_message)
    {
        size_t end = std::min(i + max_rays_per_message, rays.size());
        std::vector<models::cloud_ray> chunk(rays.begin() + i, rays.begin() + end);

        nlohmann::json rays_json;
        rays_json["rays"] = chunk;

        Aws::SNS::Model::PublishRequest publish_request;
        publish_request.SetTopicArn(topic_arn);
        publish_request.SetMessage(rays_json.dump());

        Aws::SNS::Model::MessageAttributeValue worker_id_attr;
        worker_id_attr.SetDataType("String");
        worker_id_attr.SetStringValue(target_id);
        publish_request.AddMessageAttributes("worker_id", worker_id_attr);

        Aws::SNS::Model::MessageAttributeValue source_worker_id_attr;
        source_worker_id_attr.SetDataType("String");
        source_worker_id_attr.SetStringValue(source_worker_id);
        publish_request.AddMessageAttributes("source_worker_id", source_worker_id_attr);

        auto outcome = sns_client.Publish(publish_request);

        if (!outcome.IsSuccess())
        {
            spdlog::error("Failed to publish message to SNS: {}", outcome.GetError().GetMessage());
        }
        else
        {
            spdlog::info("Message published to SNS topic {} with worker ID {}", topic_arn, target_id);
        }
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