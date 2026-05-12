#pragma once

#include "path_tracer/math/vec3.hpp"
#include "pch.hpp"
#include "vectors.hpp"

namespace models
{
struct pixel
{
    float X;
    float Y;
    math::fvec3 color;
    float alpha;
};

inline void to_json(nlohmann::json &j, const pixel &p)
{
    j = nlohmann::json{{"X", p.X}, {"Y", p.Y}, {"color", p.color}, {"alpha", p.alpha}};
}

inline void from_json(const nlohmann::json &j, pixel &p)
{
    j.at("X").get_to(p.X);
    j.at("Y").get_to(p.Y);
    j.at("color").get_to(p.color);
    j.at("alpha").get_to(p.alpha);
}
} // namespace models