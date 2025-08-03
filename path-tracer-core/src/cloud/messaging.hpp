#pragma once

#include "pch.hpp"
#include "models/cloud_ray.hpp"
#include "models/messaging.hpp"

namespace cloud
{
    void sqs_poll(const models::SQSOptions& options, std::function<void(models::cloud_ray& ray)> callback);
    void sns_send(const std::string& topic_arn, const std::string& worker_id, const models::cloud_ray& ray);
}