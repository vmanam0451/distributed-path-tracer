#pragma once

#include <map>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

struct worker_info {
    std::map<std::string, std::vector<int>> work;
    float total_size;
};

struct split_scene {
    std::map<int, worker_info> split_work;
    float total_size;
};

struct work_info {
    split_scene scene;
    std::string worker_id;
    std::string sqs_queue_arn;
    std::string sns_topic_arn;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(worker_info, work, total_size)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(split_scene, split_work, total_size)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(work_info, scene, worker_id, sqs_queue_arn, sns_topic_arn)