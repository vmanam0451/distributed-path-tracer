#pragma once 

#include "pch.hpp"
#include <path_tracer/math/vec3.hpp>

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

    inline void to_json(nlohmann::json& j, const math::fvec2& v) {
        j = nlohmann::json{
            {"x", v.x},
            {"y", v.y},
        };
    }

    inline void from_json(const nlohmann::json& j, math::fvec2& v) {
        v.x = j.at("x").get<float>();
        v.y = j.at("y").get<float>();
    }
}