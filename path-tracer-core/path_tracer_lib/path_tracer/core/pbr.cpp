#include "path_tracer/core/pbr.hpp"

#include "path_tracer/math/math.hpp"
#include "path_tracer/math/quat.hpp"
#include "path_tracer/math/mat3.hpp"
#include "path_tracer/util/rand_cone_vec.hpp"

using namespace math;

namespace core::pbr {
	// Fresnel

	static float fresnel_schlick(
		const fvec3& outcoming,
		const fvec3& incoming,
		float ior) {
		// Halfway is the normal in case of a perfectly smooth mirror
		fvec3 halfway = normalize(outcoming + incoming);
		float cos_theta = dot(outcoming, halfway);

		float f0 = (ior - 1) / (ior + 1);
		f0 *= f0;

		return lerp(f0, 1, math::pow(1 - cos_theta, 5));
	}

	// static float fresnel_reflectance(
	// 		float ior1, float cos1,
	// 		float ior2, float cos2) {
	// 	float a = ior1 * cos1;
	// 	float b = ior2 * cos2;

	// 	float r = (a - b) / (a + b);
	// 	return r * r;
	// }

	// static float fresnel_exact(
	// 		const fvec3 &normal,
	// 		const fvec3 &incoming,
	// 		float surface_ior,
	// 		float surrounding_ior) {
	// 	// Symbols used:
	// 	// i - incident angle / surrounding IOR
	// 	// t - transmitted angle / surface IOR

	// 	// From Snell's law:
	// 	// ior_i * sin_i = ior_t * sin_t

	// 	float ior_i = surrounding_ior;
	// 	float ior_t = surface_ior;

	// 	float cos_i = dot(incoming, normal);
	// 	float sin_i = math::sqrt(1 - cos_i * cos_i);

	// 	float sin_t = (ior_i * sin_i) / ior_t;

	// 	// Total internal reflection
	// 	if (sin_t > 1)
	// 		return 1;

	// 	float cos_t = math::sqrt(1 - sin_t * sin_t);

	// 	float r_s = fresnel_reflectance(ior_t, cos_i, ior_i, cos_t);
	// 	float r_p = fresnel_reflectance(ior_t, cos_t, ior_i, cos_i);

	// 	return (r_s + r_p) * 0.5F;
	// }

	// Importance

	static fvec3 importance_lambert(
		const fvec2& rand,
		const fvec3& normal,
		const fvec3& outcoming) {
		float theta = math::acos(2 * rand.x - 1) * 0.5F;
		return util::rand_cone_vec(rand.y, cos(theta), normal);
	}

	static fvec3 importance_ggx(
		const fvec2& rand,
		const fvec3& normal,
		const fvec3& outcoming,
		float roughness) {
		roughness *= roughness;
		roughness *= roughness;

		float cos_theta = math::sqrt((1 - rand.x) / (1 + (roughness - 1) * rand.x));
		fvec3 halfway = util::rand_cone_vec(rand.y, cos_theta, normal);

		return reflect(-outcoming, halfway);
	}

	// Geometric occlusion

	static float geometry_smith_g1(
		const fvec3& normal,
		const fvec3& light_dir,
		float roughness) {
		// light_dir is either outcoming or incoming
		float cos_theta = dot(normal, light_dir);
		return cos_theta / math::max(lerp(roughness, 1, cos_theta), math::epsilon);
	}

	static float geometry_smith(
		const fvec3& normal,
		const fvec3& outcoming,
		const fvec3& incoming,
		float roughness) {
		float r = roughness + 1;
		float k = (r * r) / 8;

		return geometry_smith_g1(normal, outcoming, k) *
			geometry_smith_g1(normal, incoming, k);
	}

	// Distribution

	static float distribution_lambert(
		const math::fvec3& normal,
		const math::fvec3& incoming) {
		float cos_theta = dot(normal, incoming);
		return cos_theta / math::pi;
	}

	static float distribution_ggx(
		const fvec3& normal,
		const fvec3& outcoming,
		const fvec3& incoming,
		float roughness) {
		roughness *= roughness;
		roughness *= roughness;

		fvec3 halfway = normalize(outcoming + incoming);
		float cos_phi = dot(normal, halfway);

		float denom = lerp(1, roughness, cos_phi * cos_phi);

		float cos_theta = dot(normal, incoming);
		return cos_theta * roughness / math::max(math::pi * denom * denom, math::epsilon);
	}

	// Public API

	float fresnel(
		const fvec3& outcoming,
		const fvec3& incoming,
		float ior) {
		return fresnel_schlick(outcoming, incoming, ior);
	}

	fvec3 importance_diffuse(
		const fvec2& rand,
		const fvec3& normal,
		const fvec3& outcoming) {
		return importance_lambert(rand, normal, outcoming);
	}

	fvec3 importance_specular(
		const fvec2& rand,
		const fvec3& normal,
		const fvec3& outcoming,
		float roughness) {
		return importance_ggx(rand, normal, outcoming, roughness);
	}

	float pdf_diffuse(
		const fvec3& normal,
		const fvec3& incoming) {
		return distribution_lambert(normal, incoming);
	}

	float pdf_specular(
		const fvec3& normal,
		const fvec3& outcoming,
		const fvec3& incoming,
		float roughness) {
		float dist = pbr::distribution_ggx(normal, outcoming, incoming, roughness);
		float geo = pbr::geometry_smith(normal, outcoming, incoming, roughness);

		float n_dot_o = dot(normal, outcoming);
		float n_dot_i = dot(normal, incoming);

		return (dist * geo) / math::max(4 * n_dot_o * n_dot_i, math::epsilon);
	}
}
