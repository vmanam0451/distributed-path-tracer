#pragma once

#include "geometry/ray.hpp"
#include "math/vec2.hpp"
#include "scene/component.hpp"

namespace scene {
	class camera : public component {
	public:
		geometry::ray get_ray(const math::fvec2& ndc, float ratio) const;

		float get_fov() const;

		void set_fov(float fov);

	private:
		float fov; // FOV is vertical
		float tan_half_fov;
	};
}
