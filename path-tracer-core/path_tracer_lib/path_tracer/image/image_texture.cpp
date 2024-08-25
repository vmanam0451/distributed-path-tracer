#include "path_tracer/image/image_texture.hpp"

#include "path_tracer/math/math.hpp"

using namespace math;

namespace image {
	image_texture::image_texture(const std::shared_ptr<image>& img)
		: img(img) {
	}

	std::shared_ptr<image_texture> image_texture::load(
		const std::filesystem::path& path, bool srgb) {
		return std::make_shared<image_texture>(image::load(path, srgb));
	}

	fvec4 image_texture::sample(const fvec2& coord) const {
		// Nearest

		// const uvec2 &size = img->get_size();
		// auto pixel = uvec2(coord.x * size.x, (1 - coord.y) * size.y); // % size;

		// return read_pixel(pixel);

		// Bilinear

		const uvec2& size = img->get_size();
		fvec2 center(coord.x * size.x - 0.5F, (1 - coord.y) * size.y - 0.5F);

		auto tl = mod(uvec2(floor(center.x), floor(center.y)), size);
		auto tr = mod(uvec2(ceil(center.x), floor(center.y)), size);
		auto bl = mod(uvec2(floor(center.x), ceil(center.y)), size);
		auto br = mod(uvec2(ceil(center.x), ceil(center.y)), size);

		fvec2 delta = fract(center);

		fvec4 t = lerp(read_pixel(tl), read_pixel(tr), delta.x);
		fvec4 b = lerp(read_pixel(bl), read_pixel(br), delta.x);

		return lerp(t, b, delta.y);
	}

	fvec4 image_texture::read_pixel(const uvec2& pixel) const {
		fvec4 color = fvec4::one;

		switch (img->get_channel_count()) {
		case 4:
			color.w = img->read(pixel, 3);
		case 3:
			color.z = img->read(pixel, 2);
		case 2:
			color.y = img->read(pixel, 1);
		case 1:
			color.x = img->read(pixel, 0);
		}

		return color;
	}
}
