#include "scene/camera.hpp"

#include "math/math.hpp"
#include "math/vec3.hpp"

using namespace geometry;
using namespace math;

namespace scene {
	ray camera::get_ray(const fvec2& ndc, float ratio) const {
		fvec2 dir = tan_half_fov * ndc;
		dir.x *= ratio;

		// Constructor normalizes direction
		geometry::ray ray(
			fvec3::zero,
			fvec3(dir.x, dir.y, -1)
		);

		return ray.transform(get_entity()->get_global_transform());
	}

	float camera::get_fov() const {
		return fov;
	}

	void camera::set_fov(float fov) {
		this->fov = fov;
		this->tan_half_fov = math::tan(fov * 0.5F);
	}
}
