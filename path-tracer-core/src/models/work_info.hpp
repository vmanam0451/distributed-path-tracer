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
    std::map<std::string, std::string> worker_queues;
    std::string sns_topic_arn;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(WorkerInfo, work, total_size)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SplitScene, split_work, total_size)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(WorkInfo, split_scene, worker_queues, sns_topic_arn)