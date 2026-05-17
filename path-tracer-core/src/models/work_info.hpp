#pragma once

#include "pch.hpp"

#include <array>

using mesh_name = std::string;
using primitives = std::vector<int>;
using worker_id = std::string;

namespace models
{

// scene_instance is one placement of a glTF primitive in world space.
// The preprocessor pre-bakes the world-space transform so the worker
// doesn't have to walk the node graph to compute it.
//
// world_matrix is column-major, glTF-convention.
struct scene_instance
{
    std::string mesh_name;
    int prim_idx = 0;
    std::array<float, 16> world_matrix{};
};

inline void to_json(nlohmann::json &j, const scene_instance &s)
{
    j = nlohmann::json{
        {"mesh_name", s.mesh_name},
        {"prim_idx", s.prim_idx},
        {"world_matrix", s.world_matrix},
    };
}

inline void from_json(const nlohmann::json &j, scene_instance &s)
{
    if (j.contains("mesh_name"))
        j.at("mesh_name").get_to(s.mesh_name);
    if (j.contains("prim_idx"))
        j.at("prim_idx").get_to(s.prim_idx);
    if (j.contains("world_matrix"))
        j.at("world_matrix").get_to(s.world_matrix);
}

// work_info now describes a flat list of instance placements. Two
// instances of the same mesh can be on opposite ends of the scene
// without inflating any single worker's bounding box.
struct work_info
{
    std::vector<scene_instance> instances;
    float total_size = 0.0f;
};

inline void to_json(nlohmann::json &j, const work_info &w)
{
    j = nlohmann::json{{"instances", w.instances}, {"total_size", w.total_size}};
}

inline void from_json(const nlohmann::json &j, work_info &w)
{
    if (j.contains("instances"))
        j.at("instances").get_to(w.instances);
    if (j.contains("total_size"))
        j.at("total_size").get_to(w.total_size);
}

// aabb_entry pairs a worker id with the world-space AABB of all
// geometry assigned to that worker. The full table is replicated to
// every worker so each can locally pre-filter ray dispatches.
struct aabb_entry
{
    std::string worker_id;
    std::array<float, 3> min{};
    std::array<float, 3> max{};
};

inline void to_json(nlohmann::json &j, const aabb_entry &a)
{
    j = nlohmann::json{
        {"worker_id", a.worker_id},
        {"aabb", {{"min", a.min}, {"max", a.max}}},
    };
}

inline void from_json(const nlohmann::json &j, aabb_entry &a)
{
    if (j.contains("worker_id"))
        j.at("worker_id").get_to(a.worker_id);
    if (j.contains("aabb"))
    {
        const auto &b = j.at("aabb");
        if (b.contains("min"))
            b.at("min").get_to(a.min);
        if (b.contains("max"))
            b.at("max").get_to(a.max);
    }
}

struct worker_info
{
    work_info scene_info;
    std::vector<aabb_entry> aabb_table;
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
    std::string results_queue_url;    // SQS queue URL for sending results to web app
    std::string aws_region;           // AWS region for SDK endpoint configuration
};

inline void to_json(nlohmann::json &j, const worker_info &w)
{
    j = nlohmann::json{{"scene_info", w.scene_info},
                       {"aabb_table", w.aabb_table},
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
                       {"results_queue_url", w.results_queue_url},
                       {"aws_region", w.aws_region}};
}

inline void from_json(const nlohmann::json &j, worker_info &w)
{
    if (j.contains("scene_info"))
        j.at("scene_info").get_to(w.scene_info);
    if (j.contains("aabb_table"))
        j.at("aabb_table").get_to(w.aabb_table);
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
    if (j.contains("results_queue_url"))
        j.at("results_queue_url").get_to(w.results_queue_url);
    if (j.contains("aws_region"))
        j.at("aws_region").get_to(w.aws_region);
}
} // namespace models
