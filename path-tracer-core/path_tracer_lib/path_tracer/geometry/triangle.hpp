#pragma once

#include "path_tracer/math/vec3.hpp"
#include "path_tracer/geometry/ray.hpp"

namespace geometry {
	struct triangle {
		struct intersection {
			float distance = -1;
			math::fvec3 barycentric;

			bool has_hit() const;
		};

		math::fvec3 a, b, c;

		triangle(const math::fvec3& a,
		         const math::fvec3& b,
		         const math::fvec3& c);

		math::fvec3& operator[](size_t index);

		const math::fvec3& operator[](size_t index) const;

		intersection intersect(const ray& ray) const;
	};
}
