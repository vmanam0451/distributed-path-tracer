#pragma once 

#include "pch.hpp"
#include "vectors.hpp"
#include <path_tracer/core/material.hpp>

namespace models {
    struct intersect_result {
        bool hit;
		std::shared_ptr<core::material> material;

		math::fvec3 position;
		math::fvec2 tex_coord;
		math::fvec3 normal;
		math::fvec3 tangent;
		math::fvec3 get_normal() const {
            using namespace math;
            vec3 binormal = cross(normal, tangent);
		    fmat3 tbn(tangent, binormal, normal);

		    return tbn * material->get_normal(tex_coord);
        }
	};
    
    // NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(intersect_result, hit, distance, position, normal, albedo, 
    //     opacity, roughness, metallic, emissive, ior, shadow_catcher)
}