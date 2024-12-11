#pragma once

#include "pch.hpp"

using mesh_name = std::string;
using primitives = std::vector<int>;
using worker_id = std::string;

namespace models {

    struct work_info {
        std::map<mesh_name, primitives> work;
        float total_size;
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(work_info, work, total_size)
    };

    struct worker_info {
        work_info scene_info;
        std::string scene_bucket;
        std::string scene_root;
        std::string worker_id;
        std::string sqs_queue_arn;
        std::string sns_topic_arn;
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(worker_info, scene_info, scene_bucket, scene_root, worker_id, sqs_queue_arn, sns_topic_arn)
    };
}