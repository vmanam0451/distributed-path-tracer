#include "path_tracer/scene/entity.hpp"

namespace scene {
	entity::entity() : name("entity") {
	}

	const std::string& entity::get_name() const {
		return name;
	}

	void entity::set_name(const std::string& name) {
		this->name = name;
	}

	std::shared_ptr<entity> entity::get_parent() const {
		return parent.lock();
	}

	void entity::set_parent(const
		std::shared_ptr<entity>& parent) {
		if (auto p = this->parent.lock()) {
			auto& c = p->children;
			c.erase(std::remove(c.begin(), c.end(),
			                    shared_from_this()), c.end());
		}

		this->parent = parent;

		if (parent) {
			parent->children.push_back(
				shared_from_this());
		}

		invalidate_global_transform();
	}

	const std::vector<std::shared_ptr<entity>>&
	entity::get_children() const {
		return children;
	}

	std::shared_ptr<entity> entity::find_child(
		const std::string& path) {
		auto names = util::tokenize(path, "/", true);

		auto self = shared_from_this();
		auto& current = self;

		for (std::string& name : names) {
			auto& c = current->children;
			auto it = std::find_if(c.begin(), c.end(),
			                       [name](std::shared_ptr<entity>& child) { return child->get_name() == name; });

			if (it == c.end())
				return nullptr;

			current = *it;
		}

		return current;
	}

	const transform& entity::get_local_transform() const {
		return local_transform;
	}

	void entity::set_local_transform(const transform& transform) {
		local_transform = transform;
		invalidate_global_transform();
	}

	const transform& entity::get_global_transform() {
		if (!global_transform_valid) {
			if (auto p = get_parent()) {
				global_transform_cache =
					p->get_global_transform() * local_transform;
			}
			else
				global_transform_cache = local_transform;

			global_transform_valid = true;
		}

		return global_transform_cache;
	}

	void entity::invalidate_global_transform() {
		global_transform_valid = false;

		for (auto& child : children)
			child->invalidate_global_transform();
	}

	const std::vector<std::shared_ptr<component>>
	& entity::get_components() const {
		return components;
	}
}
