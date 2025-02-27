#pragma once 

#include <path_tracer/geometry/ray.hpp>
#include "path_tracer/math/vec4.hpp"
#include "pch.hpp"
#include "vectors.hpp"
#include "intersect_result.hpp"


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
    enum ray_stage {
        INITIAL,
        DIRECT_LIGHTING,
        DIRECT_LIGHTING_RESULTS,
        INDIRECT_LIGHTING,
        COMPLETED
    };
    
    struct cloud_ray {
        std::string uuid;
        geometry::ray geometry_ray;

        geometry::ray intersect_ray;
        models::intersect_result object_intersect_result;
        models::intersect_result direct_light_intersect_result;

        math::fvec4 color;
        math::fvec3 scale;
        
        uint8_t bounce;
        ray_stage stage;
    };

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(cloud_ray, uuid, geometry_ray, intersect_ray, object_intersect_result, direct_light_intersect_result, bounce, stage)
}