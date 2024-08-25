#pragma once

#include "path_tracer/pch.hpp"

#include "path_tracer/math/mat3.hpp"
#include "path_tracer/math/quat.hpp"
#include "path_tracer/math/vec3.hpp"

namespace scene {
	class transform {
	public:
		static const transform identity;

		math::fvec3 origin;
		math::fmat3 basis;

		transform(const math::fvec3& origin = math::fvec3(0),
		          const math::fmat3& basis = math::fmat3(1));

		static transform make(
			const math::fvec3& translation = math::fvec3(0),
			const math::quat& rotation = math::quat(1),
			const math::fvec3& scale = math::fvec3(1));

		static math::fmat3 make_basis(
			const math::quat& rotation = math::quat(1),
			const math::fvec3& scale = math::fvec3(1));

		transform inverse() const;

		math::quat get_rotation() const;

		void set_rotation(const math::quat& rotation);

		void rotate(const math::quat& rotation);

		math::fvec3 get_scale() const;

		void set_scale(const math::fvec3& scale);

		void scale(const math::fvec3& scale);

		friend std::ostream& operator<<(std::ostream& lhs, const transform& rhs);

		transform& operator*=(const transform& rhs);

		transform operator*(const transform& rhs) const;

		math::fvec3 operator*(const math::fvec3& rhs) const;
	};
}
