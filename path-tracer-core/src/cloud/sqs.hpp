#pragma once

namespace cloud
{
void sqs_send_message(const std::string &queue_url, const std::string &message_body, bool terminate);
}