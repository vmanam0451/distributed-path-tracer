#pragma once

#include <path_tracer/math/vec3.hpp>

using namespace math;

namespace core {
    static float rand() {
		static std::mt19937 rng{std::random_device{}()};
		static std::uniform_real_distribution<float> dist(0, 1);

		return dist(rng);
	}

	static fvec3 rand_sphere_dir() {
		float s = rand() * 2 * math::pi;
		float t = rand() * 2 - 1;

		return fvec3(math::sin(s), math::cos(s), t) / math::sqrt(t * t + 1);
	}

	static fvec2 equirectangular_proj(const fvec3& dir) {
		return fvec2(
			math::atan2(dir.z, dir.x) * 0.1591F + 0.5F,
			math::asin(dir.y) * 0.3183F + 0.5F
		);
	}

	static fvec3 tonemap_approx_aces(const fvec3& hdr) {
		constexpr float a = 2.51F;
		constexpr fvec3 b(0.03F);
		constexpr float c = 2.43F;
		constexpr fvec3 d(0.59F);
		constexpr fvec3 e(0.14F);
		return saturate((hdr * (a * hdr + b)) / (hdr * (c * hdr + d) + e));
	}

	static fvec3 reflect(const fvec3& incident, const fvec3& normal) {
		return incident - 2 * dot(normal, incident) * normal;
	}
}