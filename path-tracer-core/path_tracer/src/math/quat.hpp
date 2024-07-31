#pragma once

#include "pch.hpp"

#include "math/math.hpp"
#include "math/mat3.hpp"
#include "math/vec3.hpp"
#include "util/string.hpp"

namespace math {
	struct quat {
		static const quat identity;

		float w;
		fvec3 v;

		quat();

		quat(float w, const fvec3& v = fvec3(0));

		quat(float w, float x, float y, float z);

		static quat axis_angle(float angle, const fvec3& axis);

		static quat from_basis(const fmat3& basis);

		// roll (Z) -> pitch (X) -> yaw (Y)
		static quat from_euler(const fvec3& euler);

		fmat3 to_basis() const;

		fvec3 to_euler() const;

#pragma region Operators

		// Quaternion + Quaternion

		quat operator+(const quat& rhs) const;

		quat operator-(const quat& rhs) const;

		quat operator*(const quat& rhs) const;

		quat operator/(const quat& rhs) const;

		quat& operator+=(const quat& rhs);

		quat& operator-=(const quat& rhs);

		quat& operator*=(const quat& rhs);

		quat& operator/=(const quat& rhs);

		// Quaternion + Scalar

		quat operator+(float rhs) const;

		quat operator-(float rhs) const;

		quat operator*(float rhs) const;

		quat operator/(float rhs) const;

		quat& operator+=(float rhs);

		quat& operator-=(float rhs);

		quat& operator*=(float rhs);

		quat& operator/=(float rhs);

		// Scalar + Quaternion

		friend quat operator+(float lhs, const quat& rhs);

		friend quat operator-(float lhs, const quat& rhs);

		friend quat operator*(float lhs, const quat& rhs);

		friend quat operator/(float lhs, const quat& rhs);

		// Quaternion + Vector

		fvec3 operator*(const fvec3& rhs) const;

		// Other

		quat& operator++();

		quat& operator--();

		quat operator++(int32_t);

		quat operator--(int32_t);

		quat operator-() const;

		bool operator==(const quat& rhs) const;

		bool operator!=(const quat& rhs) const;

		friend std::ostream& operator<<(std::ostream& lhs, const quat& rhs);

#pragma endregion
	};

	float angle(const quat& from, const quat& to);

	quat conjugate(const quat& quat);

	float dot(const quat& lhs, const quat& rhs);

	quat inverse(const quat& quat);

	template <typename Epsilon = decltype(epsilon)>
	bool is_approx(const quat& a, const quat& b,
	               Epsilon epsilon = math::epsilon);

	template <scalar Epsilon = decltype(epsilon)>
	bool is_normalized(const quat& quat,
	                   Epsilon epsilon = math::epsilon);

	float length(const quat& quat);

	quat normalize(const quat& quat);

	quat slerp(const quat& from, const quat& to, float weight);
}

#include "quat.inl"
