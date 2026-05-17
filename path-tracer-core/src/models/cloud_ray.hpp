#pragma once

#include <limits>
#include <path_tracer/geometry/ray.hpp>

#include "intersect_result.hpp"
#include "path_tracer/math/vec4.hpp"
#include "pch.hpp"
#include "vectors.hpp"

namespace geometry
{
inline void to_json(nlohmann::json &j, const geometry::ray &ray)
{
    j = nlohmann::json{{"origin", ray.origin}, {"direction", ray.get_dir()}};
}

inline void from_json(const nlohmann::json &j, geometry::ray &r)
{
    math::fvec3 origin = j.at("origin").get<math::fvec3>();
    math::fvec3 direction = j.at("direction").get<math::fvec3>();
    r = geometry::ray{origin, direction};
}
} // namespace geometry

namespace models
{

// Worker ID constants for routing
const std::string MASTER_ID = "MASTER";
const std::string WORKERS_ID = "WORKERS";
const std::string WEB_ID = "WEB";

enum ray_stage
{
    INTERSECT,
    DIRECT_LIGHTING,
    SHADING,
    ACCUMULATE
};

enum ray_type
{
    CALCULATE,
    RESOLVE,
    OWN
};

// TODO: Improve memory mangement
/*
    Don't store intersect_result directly.
    Use UUID.
        - Have different types of rays
            - Intersection Rays
            - Rays with sample
    Use UUID to map rays to a result
*/

struct cloud_ray
{
    std::string worker_id;
    uint64_t uuid;

    geometry::ray ray;
    std::optional<geometry::ray> direct_light_ray;

    float object_intersect_distance = std::numeric_limits<float>::max();
    bool direct_light_intersect_result = false;

    math::fvec3 color;
    float alpha;
    math::fvec3 scale;

    uint8_t bounce;
    ray_stage stage;
    ray_type type;
};

inline void to_json(nlohmann::json &j, const cloud_ray &r)
{
    j["worker_id"] = r.worker_id;
    j["uuid"] = r.uuid;
    j["ray"] = r.ray;

    if (r.direct_light_ray.has_value())
    {
        j["direct_light_ray"] = r.direct_light_ray.value();
    }
    else
    {
        j["direct_light_ray"] = nullptr;
    }

    j["object_intersect_distance"] = r.object_intersect_distance;
    j["direct_light_intersect_result"] = r.direct_light_intersect_result;
    j["color"] = r.color;
    j["alpha"] = r.alpha;
    j["scale"] = r.scale;
    j["bounce"] = r.bounce;
    j["stage"] = r.stage;
    j["type"] = r.type;
}

inline void from_json(const nlohmann::json &j, cloud_ray &r)
{
    j.at("worker_id").get_to(r.worker_id);
    j.at("uuid").get_to(r.uuid);
    j.at("ray").get_to(r.ray);

    if (j.contains("direct_light_ray") && !j.at("direct_light_ray").is_null())
    {
        r.direct_light_ray = j.at("direct_light_ray").get<geometry::ray>();
    }
    else
    {
        r.direct_light_ray = std::nullopt;
    }

    j.at("object_intersect_distance").get_to(r.object_intersect_distance);
    j.at("direct_light_intersect_result").get_to(r.direct_light_intersect_result);
    j.at("color").get_to(r.color);
    j.at("alpha").get_to(r.alpha);
    j.at("scale").get_to(r.scale);
    j.at("bounce").get_to(r.bounce);
    j.at("stage").get_to(r.stage);
    j.at("type").get_to(r.type);
}
} // namespace models