#pragma once

#include "models/cloud_ray.hpp"
#include "models/messaging.hpp"
#include "pch.hpp"

namespace cloud
{
void sqs_poll(const models::SQSOptions &options, std::atomic<bool> &m_should_terminate,
              std::function<void(models::cloud_ray &ray)> callback);
void sns_send(const std::string &topic_arn, const std::string &worker_id, const models::cloud_ray &ray);
void sns_signal_termination(const std::string &topic_arn, const std::string &worker_id);

} // namespace cloud