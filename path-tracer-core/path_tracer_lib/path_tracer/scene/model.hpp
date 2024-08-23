#pragma once

#include "pch.hpp"

#include "core/material.hpp"
#include "core/mesh.hpp"
#include "geometry/aabb.hpp"
#include "geometry/ray.hpp"
#include "scene/component.hpp"
#include "scene/transform.hpp"

namespace scene {
	class model : public scene::component {
	public:
		struct surface {
			std::shared_ptr<core::mesh> mesh;
			std::shared_ptr<core::material> material;
		};

		struct intersection {
			float distance = -1;
			scene::transform transform;
			const model::surface* surface;
			uint32_t triangle_index;
			math::fvec3 barycentric;

			bool has_hit() const;
		};

		std::vector<surface> surfaces;
		geometry::aabb aabb;

		void recalculate_aabb();

		intersection intersect(const geometry::ray& ray, uint8_t visualize_kd_tree_depth = 0) const;
	};
}
