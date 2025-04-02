#include "scene.hpp"
#include <path_tracer/core/utils.hpp>

using namespace core;
using namespace math;
using namespace scene;

namespace cloud {
	models::intersect_result_min distributed_scene::intersect_min_result(const geometry::ray& ray) const {
		std::stack<entity*> stack;
		for (const auto& [_, entity] : m_entities) {
			stack.push(entity.get());
		}

		model::intersection nearest_hit;

		while (!stack.empty()) {
			entity* entity = stack.top();
			stack.pop();

			for (const auto& child : entity->get_children())
				stack.push(child.get());

			if (auto model = entity->get_component<scene::model>()) {
				auto hit = model->intersect(ray);

				if (!hit.has_hit())
					continue;

				if (hit.distance < nearest_hit.distance
					|| !nearest_hit.has_hit()) {
					nearest_hit = hit;
				}
			}
		}

		if (!nearest_hit.has_hit())
			return {false, std::numeric_limits<float>::max()};

		transform transform = nearest_hit.transform;
		fmat3 normal_matrix = transpose(inverse(nearest_hit.transform.basis));


		const auto& mesh = nearest_hit.surface->mesh;
		const auto& material = nearest_hit.surface->material;

		uvec3& indices = mesh->triangles[nearest_hit.triangle_index];

		auto& v1 = mesh->vertices[indices.x];
		auto& v2 = mesh->vertices[indices.y];
		auto& v3 = mesh->vertices[indices.z];

		fvec3 position = transform * (
			v1.position * nearest_hit.barycentric.x +
			v2.position * nearest_hit.barycentric.y +
			v3.position * nearest_hit.barycentric.z);

		fvec2 tex_coord =
			v1.tex_coord * nearest_hit.barycentric.x +
			v2.tex_coord * nearest_hit.barycentric.y +
			v3.tex_coord * nearest_hit.barycentric.z;

		fvec3 normal = normalize(normal_matrix * (
			v1.normal * nearest_hit.barycentric.x +
			v2.normal * nearest_hit.barycentric.y +
			v3.normal * nearest_hit.barycentric.z));

		fvec3 tangent = normalize(normal_matrix * (
			v1.tangent * nearest_hit.barycentric.x +
			v2.tangent * nearest_hit.barycentric.y +
			v3.tangent * nearest_hit.barycentric.z));

		vec3 binormal = cross(normal, tangent);
		fmat3 tbn(tangent, binormal, normal);

		normal =  tbn * material->get_normal(tex_coord);
	
		return {true, nearest_hit.distance, position, normal};
	}


    models::intersect_result distributed_scene::intersect(const geometry::ray& ray) const {
        std::stack<entity*> stack;
		for (const auto& [_, entity] : m_entities) {
			stack.push(entity.get());
		}

		model::intersection nearest_hit;

		while (!stack.empty()) {
			entity* entity = stack.top();
			stack.pop();

			for (const auto& child : entity->get_children())
				stack.push(child.get());

			if (auto model = entity->get_component<scene::model>()) {
				auto hit = model->intersect(ray);

				if (!hit.has_hit())
					continue;

				if (hit.distance < nearest_hit.distance
					|| !nearest_hit.has_hit()) {
					nearest_hit = hit;
				}
			}
		}


		if (!nearest_hit.has_hit())
			return {false};

		const auto& mesh = nearest_hit.surface->mesh;
		const auto& material = nearest_hit.surface->material;

		uvec3& indices = mesh->triangles[nearest_hit.triangle_index];
		auto& v1 = mesh->vertices[indices.x];
		auto& v2 = mesh->vertices[indices.y];
		auto& v3 = mesh->vertices[indices.z];

		// Normals will have to be normalized if transform applies scale
		transform transform = nearest_hit.transform;
		fmat3 normal_matrix = transpose(inverse(nearest_hit.transform.basis));

		fvec3 position = transform * (
			v1.position * nearest_hit.barycentric.x +
			v2.position * nearest_hit.barycentric.y +
			v3.position * nearest_hit.barycentric.z);
		fvec2 tex_coord =
			v1.tex_coord * nearest_hit.barycentric.x +
			v2.tex_coord * nearest_hit.barycentric.y +
			v3.tex_coord * nearest_hit.barycentric.z;
		fvec3 normal = normalize(normal_matrix * (
			v1.normal * nearest_hit.barycentric.x +
			v2.normal * nearest_hit.barycentric.y +
			v3.normal * nearest_hit.barycentric.z));
		fvec3 tangent = normalize(normal_matrix * (
			v1.tangent * nearest_hit.barycentric.x +
			v2.tangent * nearest_hit.barycentric.y +
			v3.tangent * nearest_hit.barycentric.z));

		return {
			true,
			material,
			position,
			tex_coord,
			normal,
			tangent
		};
    }
}