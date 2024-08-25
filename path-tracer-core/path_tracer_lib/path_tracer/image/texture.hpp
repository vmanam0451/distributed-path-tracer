#pragma once

#include "path_tracer/math/vec2.hpp"
#include "path_tracer/math/vec4.hpp"

namespace image {
	class texture {
	public:
		virtual ~texture() {
		}

		virtual math::fvec4 sample(const math::fvec2& coord) const = 0;
	};
}
