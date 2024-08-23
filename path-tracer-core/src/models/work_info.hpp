#pragma once

#include <pch.hpp>

using mesh_name = std::string;
using primitives = std::vector<int>;
using worker_id = int;

struct worker_info {
    std::map<mesh_name, primitives> work;
    float total_size;
};

struct split_scene {
    std::map<worker_id, worker_info> split_work;
    float total_size;
};

struct work_info {
    split_scene scene;
    std::string scene_bucket;
    std::string scene_root;
    std::string worker_id;
    std::string sqs_queue_arn;
    std::string sns_topic_arn;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(worker_info, work, total_size)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(split_scene, split_work, total_size)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(work_info, scene, worker_id, sqs_queue_arn, sns_topic_arn)