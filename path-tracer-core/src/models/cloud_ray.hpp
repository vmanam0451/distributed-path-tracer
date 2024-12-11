#pragma once 

#include "pch.hpp"
#include <path_tracer/geometry/ray.hpp>

namespace math {
    inline void to_json(nlohmann::json& j, const math::fvec3& v) {
        j = nlohmann::json{
            {"x", v.x},
            {"y", v.y},
            {"z", v.z}
        };
    }

    inline void from_json(const nlohmann::json& j, math::fvec3& v) {
        v.x = j.at("x").get<float>();
        v.y = j.at("y").get<float>();
        v.z = j.at("z").get<float>();
    }
}
namespace geometry {
    inline void to_json(nlohmann::json& j, const geometry::ray& ray) {
        j = nlohmann::json{
            {"origin", ray.origin},
            {"direction", ray.get_dir()}
        };
    }    

    inline void from_json(const nlohmann::json& j, geometry::ray& r) {
        math::fvec3 origin = j.at("origin").get<math::fvec3>();
        math::fvec3 direction = j.at("direction").get<math::fvec3>();
        r = geometry::ray{origin, direction};
    }
}

namespace cloud {
    struct ray {
        geometry::ray geometry_ray;
    };

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ray, geometry_ray)
}