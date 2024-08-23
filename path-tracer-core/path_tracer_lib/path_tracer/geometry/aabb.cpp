#include "geometry/aabb.hpp"

#include "math/math.hpp"

using namespace math;

namespace geometry {
	bool aabb::intersection::has_hit() const {
		return far >= 0;
	}

	aabb::aabb() {
	}

	aabb::aabb(const fvec3& min, const fvec3& max)
		: min(min), max(max) {
	}

	void aabb::add(const fvec3& point) {
		min = math::min(min, point);
		max = math::max(max, point);
	}

	void aabb::add(const aabb& aabb) {
		add(aabb.min);
		add(aabb.max);
	}

	void aabb::clear() {
		min = fvec3(std::numeric_limits<float>::max());
		max = fvec3(std::numeric_limits<float>::min());
	}

	float aabb::get_surface_area() const {
		fvec3 widths = max - min;
		return (widths.x * widths.y +
			widths.y * widths.z +
			widths.x * widths.z) * 2;
	}

	aabb::intersection aabb::intersect(const ray& ray) const {
		if (any(min > max))
			return {};

		fvec3 inv_dir = fvec3::one / ray.get_dir();
		fvec3 min_bounds_distances = (min - ray.origin) * inv_dir;
		fvec3 max_bounds_distances = (max - ray.origin) * inv_dir;

		fvec3 near_distances = math::min(min_bounds_distances, max_bounds_distances);
		fvec3 far_distances = math::max(min_bounds_distances, max_bounds_distances);

		float near = math::max(near_distances.x,
		                       near_distances.y, near_distances.z);
		float far = math::min(far_distances.x,
		                      far_distances.y, far_distances.z);

		// Miss
		if (near > far)
			return {};

		// Near might be negative
		// if we're inside the box
		//
		// Far is negative when intersection
		// occurred behind the ray
		return {near, far};
	}
}
