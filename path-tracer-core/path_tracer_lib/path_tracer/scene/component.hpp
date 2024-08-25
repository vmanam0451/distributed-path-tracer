#pragma once

#include "path_tracer/pch.hpp"

namespace scene {
	class entity;

	class component {
	public:
		std::shared_ptr<entity> get_entity() const;

		virtual ~component() {
		}

	private:
		friend scene::entity;

		std::weak_ptr<scene::entity> parent_entity;
	};
}

#include "path_tracer/scene/entity.hpp"
