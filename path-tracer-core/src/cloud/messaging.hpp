#pragma once

#include "pch.hpp"
#include "models/cloud_ray.hpp"
#include "models/messaging_options.hpp"

namespace cloud
{
    void sqs_poll(const models::SQSOptions& options, std::function<void(const models::cloud_ray& ray)> callback);
    void sns_send(const std::string& topic_arn, const models::cloud_ray& ray);
}