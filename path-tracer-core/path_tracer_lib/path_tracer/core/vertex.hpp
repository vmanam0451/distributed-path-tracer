#pragma once

#include "path_tracer/math/vec2.hpp"
#include "path_tracer/math/vec3.hpp"

namespace core {
	struct vertex {
		math::fvec3 position;
		math::fvec2 tex_coord;
		math::fvec3 normal;
		math::fvec3 tangent;
	};
}
