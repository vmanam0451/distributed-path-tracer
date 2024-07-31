#pragma once

#include "math/vec2.hpp"
#include "math/vec3.hpp"

namespace core {
	struct vertex {
		math::fvec3 position;
		math::fvec2 tex_coord;
		math::fvec3 normal;
		math::fvec3 tangent;
	};
}
