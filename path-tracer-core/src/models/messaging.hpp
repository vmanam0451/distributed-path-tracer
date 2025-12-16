#pragma once

namespace models
{
struct SQSOptions
{
    std::string queueUrl;
    int maxMessages = 10;     // Default maximum number of messages to retrieve
    int waitTimeSeconds = 20; // Default long polling wait time
    int pollingIntervalSeconds = 3;
};

const std::string MASTER_ID = "MASTER";
const std::string WORKERS_ID = "WORKERS";

constexpr size_t SNS_MAX_MESSAGE_SIZE = 256 * 1024;
constexpr size_t SNS_OVERHEAD_BYTES = 1024; // Reserve for JSON structure overhead
constexpr size_t SNS_USABLE_SIZE = SNS_MAX_MESSAGE_SIZE - SNS_OVERHEAD_BYTES;

} // namespace models