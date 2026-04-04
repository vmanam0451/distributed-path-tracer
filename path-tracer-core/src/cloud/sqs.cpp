#include "sqs.hpp"
#include <pch.hpp>

namespace cloud
{
void sqs_send_message(const std::string &queue_url, const std::string &message_body, bool terminate)
{
    spdlog::info("Attempting to send message to SQS queue: {}", queue_url);
    Aws::SQS::SQSClient sqs_client;

    Aws::SQS::Model::SendMessageRequest send_message_request;
    send_message_request.SetQueueUrl(queue_url.c_str());
    if (terminate)
    {
        send_message_request.SetMessageAttributes(
            {{"Terminate", Aws::SQS::Model::MessageAttributeValue().WithDataType("String").WithStringValue("true")}});
    }
    else
    {
        send_message_request.SetMessageBody(message_body.c_str());
    }

    auto send_message_outcome = sqs_client.SendMessage(send_message_request);

    if (send_message_outcome.IsSuccess())
    {
        spdlog::info("Message sent to SQS queue: {}", queue_url);
    }
    else
    {
        auto error = send_message_outcome.GetError();
        spdlog::error("Error: Unable to send message to SQS queue: {}", queue_url);
        spdlog::error("Error: {}: {}", error.GetExceptionName(), error.GetMessage());
    }
}
} // namespace cloud