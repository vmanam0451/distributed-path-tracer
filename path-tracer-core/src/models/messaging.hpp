#pragma once

namespace models
{
struct SQSOptions
{
    std::string queueUrl;
    int maxMessages = 10;    // Default maximum number of messages to retrieve
    int waitTimeSeconds = 0; // Default long polling wait time
    int pollingIntervalSeconds = 3;
};

const std::string MASTER_ID = "MASTER";
const std::string WORKERS_ID = "WORKERS";
} // namespace models