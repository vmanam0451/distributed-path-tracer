#include "messaging.hpp"

using json = nlohmann::json;

namespace cloud 
{
    void sqs_poll(const models::SQSOptions& options, std::function<void(const models::cloud_ray& ray)> callback)
    {
        Aws::SQS::SQSClient sqs_client;

        Aws::SQS::Model::ReceiveMessageRequest receive_request;
        receive_request.SetQueueUrl(options.queueUrl);
        receive_request.SetMaxNumberOfMessages(options.maxMessages);
        receive_request.SetWaitTimeSeconds(options.waitTimeSeconds);

        
       
        sqs_client.ReceiveMessageAsync(receive_request, [callback](const Aws::SQS::SQSClient* client,
                                  const Aws::SQS::Model::ReceiveMessageRequest& request,
                                  const Aws::SQS::Model::ReceiveMessageOutcome& outcome,
                                  const std::shared_ptr<const Aws::Client::AsyncCallerContext>& context) {

            if (outcome.IsSuccess())
            {
                const auto& messages = outcome.GetResult().GetMessages();
                for (const auto& message : messages)
                {
                    try
                    {
                        auto ray = json::parse(message.GetBody());
                        callback(ray.get<models::cloud_ray>());
                    }
                    catch (const json::exception& e)
                    {
                        spdlog::error("JSON parsing error: {}", e.what());
                    }
                }
            }
        });
    }

    void sns_send(const std::string& topic_arn, const std::string& worker_id, const models::cloud_ray& ray) 
    {
        Aws::SNS::SNSClient sns_client;

        Aws::SNS::Model::PublishRequest publish_request;
        publish_request.SetTopicArn(topic_arn);
        publish_request.SetMessage(json(ray).dump());

        Aws::SNS::Model::MessageAttributeValue worker_id_attr;
        worker_id_attr.SetDataType("String");
        worker_id_attr.SetStringValue(worker_id);

        publish_request.AddMessageAttributes("worker_id", worker_id_attr);

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
}