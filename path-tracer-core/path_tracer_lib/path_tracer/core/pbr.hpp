#pragma once

#include "path_tracer/pch.hpp"

#include "path_tracer/math/vec2.hpp"
#include "path_tracer/math/vec3.hpp"

namespace core::pbr {
	float fresnel(
		const math::fvec3& outcoming,
		const math::fvec3& incoming,
		float ior);

	// Surround with air by default
	// float fresnel_exact(
	// 		const math::fvec3 &outcoming,
	// 		const math::fvec3 &incoming,
	// 		float surface_ior,
	// 		float surrounding_ior = 1);

	math::fvec3 importance_diffuse(
		const math::fvec2& rand,
		const math::fvec3& normal,
		const math::fvec3& outcoming);

	math::fvec3 importance_specular(
		const math::fvec2& rand,
		const math::fvec3& normal,
		const math::fvec3& outcoming,
		float roughness);

	float pdf_diffuse(
		const math::fvec3& normal,
		const math::fvec3& incoming);

	float pdf_specular(
		const math::fvec3& normal,
		const math::fvec3& outcoming,
		const math::fvec3& incoming,
		float roughness);
}
