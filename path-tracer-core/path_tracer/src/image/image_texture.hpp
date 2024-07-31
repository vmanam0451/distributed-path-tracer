#pragma once

#include "pch.hpp"

#include "image/image.hpp"
#include "image/texture.hpp"
#include "math/vec2.hpp"
#include "math/vec4.hpp"

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
