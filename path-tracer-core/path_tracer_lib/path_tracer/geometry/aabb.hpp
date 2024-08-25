#pragma once

#include "path_tracer/math/vec3.hpp"
#include "path_tracer/geometry/ray.hpp"

namespace geometry {
	struct aabb {
		struct intersection {
			float near, far = -1;

			bool has_hit() const;
		};

		math::fvec3 min, max;

		aabb();

		aabb(const math::fvec3& min, const math::fvec3& max);

		void add(const math::fvec3& point);

		void add(const aabb& aabb);

		void clear();

		float get_surface_area() const;

		intersection intersect(const ray& ray) const;
	};
}
