#include "geometry/ray.hpp"

using namespace math;

namespace geometry {
	ray::ray(const fvec3& origin, const fvec3& dir)
		: origin(origin), dir(normalize(dir)) {
	}

	ray ray::transform(const scene::transform& transform) const {
		return ray(
			transform * origin,
			transform.basis * dir
		);
	}

	math::fvec3 ray::get_dir() const {
		return dir;
	}

	void ray::set_dir(const math::fvec3& dir) {
		this->dir = normalize(dir);
	}
}
