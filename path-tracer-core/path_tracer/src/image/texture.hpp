#pragma once

#include "math/vec2.hpp"
#include "math/vec4.hpp"

namespace image {
	class texture {
	public:
		virtual ~texture() {
		}

		virtual math::fvec4 sample(const math::fvec2& coord) const = 0;
	};
}
