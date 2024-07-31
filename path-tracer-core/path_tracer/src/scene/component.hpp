#pragma once

#include "pch.hpp"

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

#include "scene/entity.hpp"
