#include "path_tracer/core/material.hpp"

using namespace math;

namespace core {
	math::fvec3 material::get_normal(const math::fvec2& coord) const {
		if (normal_tex)
			return fvec3(normal_tex->sample(coord)) * 2 - fvec3::one;
		else
			return fvec3::backward;
	}

	fvec3 material::get_albedo(const fvec2& coord) const {
		fvec3 albedo = albedo_fac;
		if (albedo_tex)
			albedo *= fvec3(albedo_tex->sample(coord));
		return albedo;
	}

	float material::get_opacity(const fvec2& coord) const {
		float opacity = opacity_fac;
		if (opacity_tex)
			opacity *= opacity_tex->sample(coord).w;
		return opacity;
	}

	float material::get_occlusion(const fvec2& coord) const {
		if (occlusion_tex)
			return occlusion_tex->sample(coord).x;
		else
			return 1;
	}

	float material::get_roughness(const fvec2& coord) const {
		float roughness = roughness_fac;
		if (roughness_tex)
			roughness *= roughness_tex->sample(coord).y;
		return roughness;
	}

	float material::get_metallic(const fvec2& coord) const {
		float metallic = metallic_fac;
		if (metallic_tex)
			metallic *= metallic_tex->sample(coord).z;
		return metallic;
	}

	fvec3 material::get_emissive(const fvec2& coord) const {
		fvec3 emissive = emissive_fac;
		if (emissive_tex)
			emissive *= fvec3(emissive_tex->sample(coord));
		return emissive;
	}
}
