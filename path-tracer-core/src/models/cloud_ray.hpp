#pragma once 

#include "pch.hpp"
#include "vectors.hpp"
#include <path_tracer/geometry/ray.hpp>


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

namespace models {
    struct cloud_ray {
        geometry::ray geometry_ray;
    };

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(cloud_ray, geometry_ray)
}