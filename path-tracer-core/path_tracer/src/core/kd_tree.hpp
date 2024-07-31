#pragma once

#include "pch.hpp"

#include "geometry/aabb.hpp"
#include "geometry/ray.hpp"
#include "geometry/triangle.hpp"

namespace core {
	class kd_tree_node {
	public:
		virtual ~kd_tree_node() {
		}

	protected:
		kd_tree_node() {
		}
	};

	class kd_tree_branch : public kd_tree_node {
	public:
		uint8_t axis;
		float split;
		std::shared_ptr<kd_tree_node> left, right;
	};

	class kd_tree_leaf : public kd_tree_node {
	public:
		std::vector<geometry::triangle> triangles;
		std::vector<uint32_t> indices;
	};
}
