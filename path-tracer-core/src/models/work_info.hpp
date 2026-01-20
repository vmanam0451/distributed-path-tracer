#pragma once

#include "pch.hpp"

using mesh_name = std::string;
using primitives = std::vector<int>;
using worker_id = std::string;

namespace models
{

struct work_info
{
    std::map<mesh_name, primitives> work;
    float total_size = 0.0f;
};

inline void to_json(nlohmann::json &j, const work_info &w)
{
    j = nlohmann::json{{"work", w.work}, {"total_size", w.total_size}};
}

inline void from_json(const nlohmann::json &j, work_info &w)
{
    if (j.contains("work"))
        j.at("work").get_to(w.work);
    if (j.contains("total_size"))
        j.at("total_size").get_to(w.total_size);
}

struct worker_info
{
    work_info scene_info;
    std::string scene_bucket;
    std::string scene_root;
    std::string worker_id;
    int num_workers = 0;

    int samples = 0;
    int bounces = 0;
    float min_x = 0.0f;
    float min_y = 0.0f;
    float max_x = 0.0f;
    float max_y = 0.0f;
    int image_width = 640;
    int image_height = 480;

    // Cloud Map configuration for direct TCP communication
    std::string cloud_map_namespace;
    std::string cloud_map_service;
    std::string cloud_map_service_id; // Service ID needed for RegisterInstance API
    std::string aws_region;           // AWS region for SDK endpoint configuration
};

inline void to_json(nlohmann::json &j, const worker_info &w)
{
    j = nlohmann::json{{"scene_info", w.scene_info},
                       {"scene_bucket", w.scene_bucket},
                       {"scene_root", w.scene_root},
                       {"worker_id", w.worker_id},
                       {"num_workers", w.num_workers},
                       {"samples", w.samples},
                       {"bounces", w.bounces},
                       {"min_x", w.min_x},
                       {"min_y", w.min_y},
                       {"max_x", w.max_x},
                       {"max_y", w.max_y},
                       {"image_width", w.image_width},
                       {"image_height", w.image_height},
                       {"cloud_map_namespace", w.cloud_map_namespace},
                       {"cloud_map_service", w.cloud_map_service},
                       {"cloud_map_service_id", w.cloud_map_service_id},
                       {"aws_region", w.aws_region}};
}

inline void from_json(const nlohmann::json &j, worker_info &w)
{
    if (j.contains("scene_info"))
        j.at("scene_info").get_to(w.scene_info);
    if (j.contains("scene_bucket"))
        j.at("scene_bucket").get_to(w.scene_bucket);
    if (j.contains("scene_root"))
        j.at("scene_root").get_to(w.scene_root);
    if (j.contains("worker_id"))
        j.at("worker_id").get_to(w.worker_id);
    if (j.contains("num_workers"))
        j.at("num_workers").get_to(w.num_workers);
    if (j.contains("samples"))
        j.at("samples").get_to(w.samples);
    if (j.contains("bounces"))
        j.at("bounces").get_to(w.bounces);
    if (j.contains("min_x"))
        j.at("min_x").get_to(w.min_x);
    if (j.contains("min_y"))
        j.at("min_y").get_to(w.min_y);
    if (j.contains("max_x"))
        j.at("max_x").get_to(w.max_x);
    if (j.contains("max_y"))
        j.at("max_y").get_to(w.max_y);
    if (j.contains("image_width"))
        j.at("image_width").get_to(w.image_width);
    if (j.contains("image_height"))
        j.at("image_height").get_to(w.image_height);
    if (j.contains("cloud_map_namespace"))
        j.at("cloud_map_namespace").get_to(w.cloud_map_namespace);
    if (j.contains("cloud_map_service"))
        j.at("cloud_map_service").get_to(w.cloud_map_service);
    if (j.contains("cloud_map_service_id"))
        j.at("cloud_map_service_id").get_to(w.cloud_map_service_id);
    if (j.contains("aws_region"))
        j.at("aws_region").get_to(w.aws_region);
}
} // namespace models