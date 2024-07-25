#pragma once

#include "pch.hpp"

#include "core/material.hpp"
#include "core/mesh.hpp"
#include "image/texture.hpp"
#include "math/vec2.hpp"
#include "math/vec3.hpp"
#include "scene/camera.hpp"
#include "scene/entity.hpp"
#include "scene/sun_light.hpp"

namespace core {
	class renderer {
	public:
		~renderer();

		static constexpr uint32_t no_sun_light = static_cast<uint32_t>(-1);

		math::uvec2 resolution = math::fvec2(1920, 1080);
		uint32_t thread_count = 0;
		uint32_t sample_count = 10000;
		uint8_t bounce_count = 4;
		std::unordered_map<std::string, std::shared_ptr<scene::entity>> entities;
		std::shared_ptr<scene::entity> camera;
		std::shared_ptr<scene::entity> sun_light;
		std::shared_ptr<image::texture> environment;
		math::fvec3 environment_factor = math::fvec3::one;
		bool transparent_background = false;
		uint32_t camera_index = 0; // TODO Move to load_gltf
		uint32_t sun_light_index = 0; // TODO Move to load_gltf
		uint8_t visualize_kd_tree_depth = 0; // 0 = disabled

		void load_gltf(const std::filesystem::path& path);
		void render(const std::filesystem::path& path) const;

	private:
		struct intersect_result {
			bool hit;
			std::shared_ptr<core::material> material;

			math::fvec3 position;
			math::fvec2 tex_coord;
			math::fvec3 normal;
			math::fvec3 tangent;

			math::fvec3 get_normal() const;
		};

		math::fvec4 trace(uint8_t bounce, const geometry::ray& ray) const;

		intersect_result intersect(const geometry::ray& ray) const;

		std::shared_ptr<mesh> get_mesh(cgltf_primitive* primitive, const std::filesystem::path& path);
		std::shared_ptr<material>  get_material(cgltf_primitive* primitive, const std::filesystem::path& path);

		void process_node(cgltf_node* cgltf_node, cgltf_camera* cgltf_camera, cgltf_light* cgltf_sun_light, scene::entity* parent, const std::filesystem::path& path);
	private:
		cgltf_data* data = nullptr;
	};
}
