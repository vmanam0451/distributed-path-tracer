#pragma once

#include "path_tracer/pch.hpp"

#include "path_tracer/image/image.hpp"
#include "path_tracer/image/texture.hpp"
#include "path_tracer/math/vec2.hpp"
#include "path_tracer/math/vec4.hpp"

namespace image {
	class image_texture : public texture {
	public:
		image_texture(const std::shared_ptr<image>& img);

		static std::shared_ptr<image_texture> load(const std::filesystem::path& path, bool srgb);

		math::fvec4 sample(const math::fvec2& coord) const override;

	private:
		std::shared_ptr<image> img;

		math::fvec4 read_pixel(const math::uvec2& pixel) const;
	};
}
