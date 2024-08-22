#pragma once

#include <map>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

struct WorkerInfo {
    std::map<std::string, std::vector<int>> work;
    float total_size;
};

struct SplitScene {
    std::map<int, WorkerInfo> split_work;
    float total_size;
};

struct WorkInfo {
    SplitScene split_scene;
    std::string worker_id;
    std::string sqs_queue_arn;
    std::string sns_topic_arn;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(WorkerInfo, work, total_size)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SplitScene, split_work, total_size)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(WorkInfo, split_scene, worker_id, sqs_queue_arn, sns_topic_arn)