#pragma once

#include "path_tracer/pch.hpp"

#include "path_tracer/math/vec3.hpp"

namespace util {
	math::fvec3 rand_cone_vec(float rand, float cos_theta, math::fvec3 normal);
}
