#pragma once

#include "path_tracer/math/vec3.hpp"
#include "path_tracer/scene/component.hpp"

namespace scene {
	class sun_light : public component {
	public:
		math::fvec3 energy = math::fvec3(1, 0.6, 0.2) * 50;
		float angular_radius = 0.004732; // In radians
	};
}
