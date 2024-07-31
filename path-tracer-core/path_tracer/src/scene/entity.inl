namespace scene {
	template <std::derived_from<component> T>
	std::shared_ptr<T> entity::get_component() const {
		if (component_index.contains(typeid(T))) {
			size_t index = component_index.at(typeid(T));
			return std::static_pointer_cast<T>(components[index]);
		}
		else
			return nullptr;
	}

	template <std::derived_from<component> T>
	std::shared_ptr<T> entity::add_component() {
		if (component_index.contains(typeid(T)))
			return nullptr;

		auto component = std::make_shared<T>();
		component->parent_entity = shared_from_this();

		component_index.emplace(typeid(T), components.size());
		components.push_back(component);

		return component;
	}

	template <std::derived_from<component> T>
	std::shared_ptr<T> entity::remove_component() {
		if (!component_index.contains(typeid(T)))
			return nullptr;

		auto it = components.begin() + component_index[typeid(T)];
		auto component = *it;

		components.erase(it);
		component_index.erase(typeid(T));

		component->parent_entity.reset();
		return std::static_pointer_cast<T>(component);
	}
}
