#pragma once

#include "pch.hpp"

#include "image/texture.hpp"
#include "math/vec3.hpp"

namespace core {
	class material {
	public:
		math::fvec3 albedo_fac = math::fvec3::one;
		float opacity_fac = 1;
		float roughness_fac = 1;
		float metallic_fac = 1;
		math::fvec3 emissive_fac = 1;
		float ior = 1.33F;
		bool shadow_catcher = false;

		std::shared_ptr<image::texture>
			normal_tex = nullptr,
			albedo_tex = nullptr,
			opacity_tex = nullptr,
			occlusion_tex = nullptr,
			roughness_tex = nullptr,
			metallic_tex = nullptr,
			emissive_tex = nullptr;

		math::fvec3 get_normal(const math::fvec2& coord) const;

		math::fvec3 get_albedo(const math::fvec2& coord) const;

		float get_opacity(const math::fvec2& coord) const;

		float get_occlusion(const math::fvec2& coord) const;

		float get_roughness(const math::fvec2& coord) const;

		float get_metallic(const math::fvec2& coord) const;

		math::fvec3 get_emissive(const math::fvec2& coord) const;
	};
}
