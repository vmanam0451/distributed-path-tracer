#include "path_tracer/util/rand_cone_vec.hpp"

#include "path_tracer/math/mat3.hpp"

using namespace math;

namespace util {
	fvec3 rand_cone_vec(float rand, float cos_theta, fvec3 normal) {
		// Generate random vector in a Z-oriented cone

		float phi = rand * 2 * math::pi;
		float sin_theta = sqrt(1 - cos_theta * cos_theta);

		fvec3 cone_vec(
			math::cos(phi) * sin_theta,
			math::sin(phi) * sin_theta,
			cos_theta
		);

		// Create coordinate system such that Z = normal

		fvec3 non_parallel_vec = fvec3::zero;
		if (abs(normal.x) < (1 / math::sqrt3))
			non_parallel_vec.x = 1;
		else if (abs(normal.y) < (1 / math::sqrt3))
			non_parallel_vec.y = 1;
		else
			non_parallel_vec.z = 1;

		fvec3 tangent = normalize(cross(normal, non_parallel_vec));
		fvec3 binormal = cross(normal, tangent);
		fmat3 tbn(tangent, binormal, normal);

		return tbn * cone_vec;
	}
}
