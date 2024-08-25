#pragma once

#include "path_tracer/pch.hpp"

#include "path_tracer/scene/component.hpp"
#include "path_tracer/scene/transform.hpp"

namespace scene {
	class entity : public std::enable_shared_from_this<entity> {
	public:
		entity();

		const std::string& get_name() const;

		void set_name(const std::string& name);

		std::shared_ptr<entity> get_parent() const;

		void set_parent(const std::shared_ptr<entity>& parent);

		const std::vector<std::shared_ptr<entity>>& get_children() const;

		std::shared_ptr<entity> find_child(const std::string& path);

		const transform& get_local_transform() const;

		void set_local_transform(const transform& transform);

		const transform& get_global_transform();

		const std::vector<std::shared_ptr<component>>& get_components() const;

		template <std::derived_from<component> T>
		std::shared_ptr<T> get_component() const;

		template <std::derived_from<component> T>
		std::shared_ptr<T> add_component();

		template <std::derived_from<component> T>
		std::shared_ptr<T> remove_component();

	private:
		std::string name;

		std::weak_ptr<entity> parent;
		std::vector<std::shared_ptr<entity>> children;

		transform local_transform;
		transform global_transform_cache;
		bool global_transform_valid = false;

		std::vector<std::shared_ptr<component>> components;
		std::unordered_map<std::type_index, size_t> component_index;

		void invalidate_global_transform();
	};
}

#include "path_tracer/scene/entity.inl"
