#pragma once 

#include "pch.hpp"
#include "vectors.hpp"

namespace models {
    struct intersect_result {
        bool hit;
		float distance;

        math::fvec3 position;
		math::fvec2 tex_coord;
		math::fvec3 normal;

        math::fvec3 albedo;
        float opacity;
        float roughness;
        float metallic;
        math::fvec3 emissive;
        float ior;

        NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(intersect_result, hit, distance, albedo, opacity, roughness, metallic, emissive, ior, position, tex_coord, normal)
	};
}