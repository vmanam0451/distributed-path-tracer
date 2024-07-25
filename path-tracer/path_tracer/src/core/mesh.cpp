#include "core/mesh.hpp"

#include "geometry/triangle.hpp"

using namespace geometry;
using namespace math;

namespace core {
	namespace kd_tree_builder {
		static kd_tree_node* init_leaf(
			std::vector<triangle>&& triangles,
			std::vector<uint32_t>&& indices) {
			auto node = new kd_tree_leaf;

			(node->triangles = std::move(triangles)).shrink_to_fit();
			(node->indices = std::move(indices)).shrink_to_fit();

			return node;
		}

		static std::tuple<aabb, aabb> split_aabb(
			const aabb& aabb,
			uint8_t axis, float split) {
			geometry::aabb laabb, raabb;
			laabb = raabb = aabb;

			// Left node contains objects
			// with smaller position
			laabb.max[axis] = split;
			raabb.min[axis] = split;

			return {laabb, raabb};
		}

		static std::tuple<
			std::vector<triangle>, std::vector<triangle>,
			std::vector<uint32_t>, std::vector<uint32_t>
		> split_triangles(
			const std::vector<triangle>& triangles,
			const std::vector<uint32_t>& indices,
			uint8_t axis, float split) {
			// Split triangles and their indices
			std::vector<triangle> ltriangles, rtriangles;
			ltriangles.reserve(triangles.size());
			rtriangles.reserve(triangles.size());

			std::vector<uint32_t> lindices, rindices;
			lindices.reserve(indices.size());
			rindices.reserve(indices.size());

			for (size_t i = 0; i < triangles.size(); i++) {
				const triangle& triangle = triangles[i];
				uint32_t index = indices[i];

				bool lassign = false, rassign = false;

				for (size_t i = 0; i < 3; i++) {
					const fvec3& vertex = triangle[i];
					if (vertex[axis] < split)
						lassign = true;
					else
						rassign = true;
				}

				if (lassign) {
					ltriangles.push_back(triangle);
					lindices.push_back(index);
				}

				if (rassign) {
					rtriangles.push_back(triangle);
					rindices.push_back(index);
				}
			}

			return {
				ltriangles, rtriangles,
				lindices, rindices
			};
		}

		static kd_tree_node* init_node_median(
			aabb&& aabb,
			std::vector<triangle>&& triangles,
			std::vector<uint32_t>&& indices,
			uint8_t depth) {
			// Create leaf node once
			// we've reached maximum depth
			if (depth == 0) {
				return init_leaf(std::move(triangles),
				                 std::move(indices));
			}

			auto node = new kd_tree_branch;

			// We split in the middle
			fvec3 widths = aabb.max - aabb.min;
			node->axis = std::max_element(&widths.x,
			                              &widths.x + 3) - &widths.x;
			node->split = aabb.min[node->axis] +
				widths[node->axis] * 0.5F;

			auto [laabb, raabb] = split_aabb(aabb,
			                                 node->axis, node->split);

			auto [ltriangles, rtriangles,
					lindices, rindices] =
				split_triangles(triangles,
				                indices, node->axis, node->split);

			if (ltriangles.size() > 0) {
				node->left.reset(init_node_median(
					std::move(laabb),
					std::move(ltriangles),
					std::move(lindices),
					depth - 1));
			}

			if (rtriangles.size() > 0) {
				node->right.reset(init_node_median(
					std::move(raabb),
					std::move(rtriangles),
					std::move(rindices),
					depth - 1));
			}

			return node;
		}

		// Alternative to init_node_median
		kd_tree_node* init_node_sah(
			aabb&& aabb,
			std::vector<triangle>&& triangles,
			std::vector<uint32_t>&& indices,
			uint8_t depth) {
			// Create leaf node once
			// we've reached maximum depth
			if (depth == 0)
				return init_leaf(std::move(triangles), std::move(indices));

			fvec3 widths = aabb.max - aabb.min;

			float base_cost = triangles.size() * aabb.get_surface_area();
			float best_cost = base_cost;
			uint8_t best_axis;
			float best_split;

			std::vector<std::tuple<float, bool>> bounds;
			bounds.reserve(triangles.size() * 2);

			for (uint8_t axis = 0; axis < 3; axis++) {
				bounds.clear();

				for (triangle& triangle : triangles) {
					float start = math::min(triangle.a[axis], triangle.b[axis], triangle.c[axis]);
					float end = math::max(triangle.a[axis], triangle.b[axis], triangle.c[axis]);

					bounds.emplace_back(start, true);
					bounds.emplace_back(end, false);
				}

				std::sort(bounds.begin(), bounds.end(),
				          [](auto& x, auto& y) { return get<0>(x) < get<0>(y); });

				float split;
				uint32_t lcount = 0;
				uint32_t rcount = triangles.size();

				for (size_t i = 0; i <= bounds.size(); i++) {
					if (i == 0)
						split = get<0>(bounds.front()) - math::epsilon;
					else if (i == bounds.size()) {
						// Last event is always END
						rcount--;
						split = get<0>(bounds.back()) + math::epsilon;
					}
					else {
						auto& prev_bound = bounds[i - 1];
						auto& next_bound = bounds[i];

						if (get<1>(prev_bound))
							lcount++;
						else
							rcount--;

						if (get<0>(prev_bound) == get<0>(next_bound))
							continue;

						split = (get<0>(prev_bound) + get<0>(next_bound)) * 0.5F;
					}

					// Accumulate events until we enter the AABB
					if (split <= aabb.min[axis])
						continue;

					if (split >= aabb.max[axis])
						break;

					auto [laabb, raabb] = split_aabb(
						aabb, axis, split);

					float cost = lcount * laabb.get_surface_area()
						+ rcount * raabb.get_surface_area();

					if (cost < best_cost) {
						best_cost = cost;
						best_axis = axis;
						best_split = split;
					}
				}
			}

			if (best_cost < base_cost) {
				auto node = new kd_tree_branch;

				node->axis = best_axis;
				node->split = best_split;

				auto [laabb, raabb] = split_aabb(aabb,
				                                 node->axis, node->split);

				auto [ltriangles, rtriangles,
						lindices, rindices] =
					split_triangles(triangles, indices,
					                node->axis, node->split);

				if (ltriangles.size() > 0) {
					node->left.reset(kd_tree_builder::init_node_sah(
						std::move(laabb),
						std::move(ltriangles),
						std::move(lindices),
						depth - 1));
				}

				if (rtriangles.size() > 0) {
					node->right.reset(kd_tree_builder::init_node_sah(
						std::move(raabb),
						std::move(rtriangles),
						std::move(rindices),
						depth - 1));
				}

				return node;
			}
			else
				return init_leaf(std::move(triangles), std::move(indices));
		}
	}

	bool mesh::intersection::has_hit() const {
		return distance >= 0;
	}

	void mesh::recalculate_aabb() {
		aabb.clear();
		for (vertex& v : vertices)
			aabb.add(v.position);

		aabb.min -= fvec3(math::epsilon);
		aabb.max += fvec3(math::epsilon);
	}

	void mesh::build_kd_tree(bool use_sah, uint8_t max_depth) {
		// Convert root vertices to triangles
		std::vector<triangle> triangles;
		triangles.reserve(this->triangles.size());

		for (size_t i = 0; i < this->triangles.size(); i++) {
			uvec3 indices = this->triangles[i];
			triangles.push_back(triangle(
				vertices[indices.x].position,
				vertices[indices.y].position,
				vertices[indices.z].position
			));
		}

		// Initialize root triangle indices
		std::vector<uint32_t> indices(this->triangles.size());
		std::iota(indices.begin(), indices.end(), 0);

		std::cout << "Building kD tree..." << std::endl;

		// Start executing initial job
		if (use_sah) {
			kd_tree.reset(kd_tree_builder::init_node_sah(
				std::move(aabb),
				std::move(triangles),
				std::move(indices),
				max_depth));
		}
		else {
			kd_tree.reset(kd_tree_builder::init_node_median(
				std::move(aabb),
				std::move(triangles),
				std::move(indices),
				max_depth));
		}
	}

	mesh::intersection mesh::intersect(const ray& ray, uint8_t visualize_kd_tree_depth) const {
		auto result = aabb.intersect(ray);
		if (!result.has_hit())
			return {};

		// uint8_t depth is only used for tree visualization 
		std::stack<std::tuple<const kd_tree_node*, float, float, uint8_t>> stack;
		stack.push(std::make_tuple(kd_tree.get(), result.near, result.far, 1));

		while (!stack.empty()) {
			auto [node, min_dist, max_dist, depth] = stack.top();
			stack.pop();

			// Explore down the tree until we reach a leaf
			while (node && typeid(*node) == typeid(kd_tree_branch)) {
				if (depth++ == visualize_kd_tree_depth) {
					size_t ptr_val = reinterpret_cast<size_t>(node);
					std::mt19937 rng{static_cast<unsigned int>(ptr_val)};
					float hue = std::uniform_real_distribution<float>{0, 1}(rng);

					fvec3 color = math::saturate(fvec3(
						math::abs(hue * 6 - 3) - 1,
						2 - math::abs(hue * 6 - 2),
						2 - math::abs(hue * 6 - 4)
					));

					return {
						min_dist,
						color, // Random color instead of barycentric coordinates
						static_cast<uint32_t>(-1) // No index
					};
				}

				auto branch = static_cast<const kd_tree_branch*>(node);

				// Distance to the split plane
				float split_dist = (branch->split - ray.origin[branch
					->axis]) / ray.get_dir()[branch->axis];

				const kd_tree_node *first, *second;

				if (ray.origin[branch->axis] < branch->split) {
					first = branch->left.get();
					second = branch->right.get();
				}
				else {
					first = branch->right.get();
					second = branch->left.get();
				}

				// If ray points away from the split plane
				// or if the back of the AABB is closer
				// than distance to the split plane,
				// we've hit just the first node
				if (split_dist < 0 || split_dist > max_dist)
					node = first;

					// When node's AABB is further away than the split plane
					// then we've hit second node only
				else if (split_dist < min_dist)
					node = second;

				// Otherwise we've hit them both
				else {
					if (second)
						stack.push(std::make_tuple(second, split_dist, max_dist, depth));

					node = first;
					max_dist = split_dist;
				}
			}

			if (!node)
				continue;

			// It's a leaf node
			auto leaf = static_cast<const kd_tree_leaf*>(node);

			triangle::intersection nearest_hit;
			uint32_t index = 0;

			for (uint32_t i = 0; i < leaf->triangles.size(); i++) {
				auto hit = leaf->triangles[i].intersect(ray);
				if (hit.has_hit() && hit.distance <= max_dist &&
					(hit.distance < nearest_hit.distance ||
						!nearest_hit.has_hit())) {
					nearest_hit = hit;
					index = i;
				}
			}

			if (!nearest_hit.has_hit())
				continue;

			if (visualize_kd_tree_depth != 0)
				return {};

			return {
				nearest_hit.distance,
				nearest_hit.barycentric,
				leaf->indices[index]
			};
		}

		return {};
	}
}
