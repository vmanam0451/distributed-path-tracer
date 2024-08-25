#pragma once

#include "path_tracer/pch.hpp"

#include "path_tracer/core/kd_tree.hpp"
#include "path_tracer/core/material.hpp"
#include "path_tracer/core/vertex.hpp"
#include "path_tracer/geometry/ray.hpp"
#include "path_tracer/math/vec2.hpp"
#include "path_tracer/math/vec3.hpp"

namespace core {
	struct mesh {
		struct intersection {
			float distance = -1;
			math::fvec3 barycentric;
			uint32_t index;

			bool has_hit() const;
		};

		std::vector<vertex> vertices;
		std::vector<math::uvec3> triangles;
		geometry::aabb aabb;
		std::shared_ptr<kd_tree_node> kd_tree = nullptr;
		std::shared_ptr<core::material> material = nullptr;

		// void recalculate_normals(bool shade_smooth = false);

		// void recalculate_tangents();

		void recalculate_aabb();

		void build_kd_tree(bool use_sah = true, uint8_t max_depth = 25);

		intersection intersect(const geometry::ray& ray, uint8_t visualize_kd_tree_depth = 0) const;
	};
}
