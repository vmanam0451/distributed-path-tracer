#include "math/quat.hpp"

namespace math {
	const quat quat::identity(1);

	quat::quat() : quat(0) {
	}

	quat::quat(float w, const fvec3& v)
		: w(w), v(v) {
	}

	quat::quat(float w, float x, float y, float z)
		: w(w), v(x, y, z) {
	}

	quat quat::axis_angle(float angle, const fvec3& axis) {
		float half_angle = angle * 0.5F;

		return quat(
			cos(half_angle),
			sin(half_angle) * normalize(axis)
		);
	}

	quat quat::from_basis(const fmat3& basis) {
		assert(is_orthonormal(basis) && "Basis must be orthonormal.");

		fvec3 x = basis.x, y = basis.y, z = basis.z;

		float t;
		quat q;

		if (z.z < 0) {
			if (x.x > y.y) {
				t = 1 + x.x - y.y - z.z;
				q = quat(y.z - z.y, t, x.y + y.x, z.x + x.z);
			}
			else {
				t = 1 - x.x + y.y - z.z;
				q = quat(z.x - x.z, x.y + y.x, t, y.z + z.y);
			}
		}
		else {
			if (x.x < -y.y) {
				t = 1 - x.x - y.y + z.z;
				q = quat(x.y - y.x, z.x + x.z, y.z + z.y, t);
			}
			else {
				t = 1 + x.x + y.y + z.z;
				q = quat(t, y.z - z.y, z.x - x.z, x.y - y.x);
			}
		}

		return q * (0.5F / sqrt(t));
	}

	quat quat::from_euler(const fvec3& euler) {
		fvec3 half_euler = euler * 0.5F;

		float sp = sin(half_euler.x);
		float sy = sin(half_euler.y);
		float sr = sin(half_euler.z);

		float cp = cos(half_euler.x);
		float cy = cos(half_euler.y);
		float cr = cos(half_euler.z);

		return quat(
			cr * cp * cy + sr * sp * sy,
			cr * sp * cy + sr * cp * sy,
			cr * cp * sy - sr * sp * cy,
			sr * cp * cy - cr * sp * sy
		);
	}

	fvec3 quat::to_euler() const {
		float xx = v.x * v.x;

		float pitch_y = 2 * (w * v.x - v.y * v.z);
		pitch_y = clamp(pitch_y, -1, 1);
		float pitch = asin(pitch_y);

		float yaw_y = 2 * (w * v.y + v.z * v.x);
		float yaw_x = 1 - 2 * (v.y * v.y + xx);
		float yaw = atan2(yaw_y, yaw_x);

		float roll_y = 2 * (w * v.z + v.x * v.y);
		float roll_x = 1 - 2 * (v.z * v.z + xx);
		float roll = atan2(roll_y, roll_x);

		return fvec3(pitch, yaw, roll);
	}

	fmat3 quat::to_basis() const {
		// return fmat3(
		// 	*this * fvec3::RIGHT,
		// 	*this * fvec3::UP,
		// 	*this * fvec3::BACKWARD
		// );

		return fmat3(
			1 - 2 * (v.y * v.y + v.z * v.z),
			2 * (v.x * v.y + v.z * w),
			2 * (v.x * v.z - v.y * w),
			2 * (v.x * v.y - v.z * w),
			1 - 2 * (v.x * v.x + v.z * v.z),
			2 * (v.y * v.z + v.x * w),
			2 * (v.x * v.z + v.y * w),
			2 * (v.y * v.z - v.x * w),
			1 - 2 * (v.x * v.x + v.y * v.y)
		);
	}

#pragma region Operators

	// Quaternion + Quaternion

	quat quat::operator+(const quat& rhs) const {
		return quat(w + rhs.w, v + rhs.v);
	}

	quat quat::operator-(const quat& rhs) const {
		return quat(w - rhs.w, v - rhs.v);
	}

	quat quat::operator*(const quat& rhs) const {
		return quat(
			w * rhs.w - v.x * rhs.v.x - v.y * rhs.v.y - v.z * rhs.v.z,
			w * rhs.v.x + v.x * rhs.w + v.y * rhs.v.z - v.z * rhs.v.y,
			w * rhs.v.y - v.x * rhs.v.z + v.y * rhs.w + v.z * rhs.v.x,
			w * rhs.v.z + v.x * rhs.v.y - v.y * rhs.v.x + v.z * rhs.w
		);
	}

	quat quat::operator/(const quat& rhs) const {
		return *this * inverse(rhs);
	}

	quat& quat::operator+=(const quat& rhs) {
		return *this = *this + rhs;
	}

	quat& quat::operator-=(const quat& rhs) {
		return *this = *this - rhs;
	}

	quat& quat::operator*=(const quat& rhs) {
		return *this = *this * rhs;
	}

	quat& quat::operator/=(const quat& rhs) {
		return *this = *this / rhs;
	}

	// Quaternion + Scalar

	quat quat::operator+(float rhs) const {
		return quat(w + rhs, v);
	}

	quat quat::operator-(float rhs) const {
		return quat(w - rhs, v);
	}

	quat quat::operator*(float rhs) const {
		return quat(w * rhs, v * rhs);
	}

	quat quat::operator/(float rhs) const {
		return quat(w / rhs, v / rhs);
	}

	quat& quat::operator+=(float rhs) {
		return *this = *this + rhs;
	}

	quat& quat::operator-=(float rhs) {
		return *this = *this - rhs;
	}

	quat& quat::operator*=(float rhs) {
		return *this = *this * rhs;
	}

	quat& quat::operator/=(float rhs) {
		return *this = *this / rhs;
	}

	// Scalar + Quaternion

	quat operator+(float lhs, const quat& rhs) {
		return quat(lhs + rhs.w, rhs.v);
	}

	quat operator-(float lhs, const quat& rhs) {
		return quat(lhs - rhs.w, -rhs.v);
	}

	quat operator*(float lhs, const quat& rhs) {
		return quat(lhs * rhs.w, lhs * rhs.v);
	}

	quat operator/(float lhs, const quat& rhs) {
		return lhs * inverse(rhs);
	}

	// Quaternion + Vector

	fvec3 quat::operator*(const fvec3& rhs) const {
		// return (*this * quat(0, rhs) * inverse(*this)).v;

		fvec3 c = cross(v, rhs);
		return rhs + c * (2 * w) + cross(v, c) * 2;
	}

	// Other

	quat& quat::operator++() {
		w++;
		return *this;
	}

	quat& quat::operator--() {
		w--;
		return *this;
	}

	quat quat::operator++(int32_t) {
		quat q = *this;
		++(*this);
		return q;
	}

	quat quat::operator--(int32_t) {
		quat q = *this;
		--(*this);
		return q;
	}

	quat quat::operator-() const {
		return quat(-w, -v);
	}

	bool quat::operator==(const quat& rhs) const {
		return w == rhs.w && all(v == rhs.v);
	}

	bool quat::operator!=(const quat& rhs) const {
		return w != rhs.w || any(v != rhs.v);
	}

	std::ostream& operator<<(std::ostream& lhs, const quat& rhs) {
		bool first_var = true;

		for (int32_t i = 0; i < 4; i++) {
			float var;
			if (i == 0)
				var = rhs.w;
			else
				var = rhs.v[i - 1];

			if (var != 0) {
				if (var > 0) {
					if (!first_var)
						lhs << " + ";
				}
				else {
					if (!first_var)
						lhs << " - ";
					else
						lhs << '-';
				}

				var = abs(var);
				if (i == 0 || !is_approx(var, 1))
					lhs << util::to_string(var);

				if (i != 0)
					lhs << static_cast<char>('i' + (i - 1));

				first_var = false;
			}
		}

		if (first_var)
			lhs << '0';

		return lhs;
	}

#pragma endregion

	float angle(const quat& from, const quat& to) {
		return acos(dot(normalize(from), normalize(to)));
	}

	quat conjugate(const quat& quat) {
		return math::quat(quat.w, -quat.v);
	}

	float dot(const quat& lhs, const quat& rhs) {
		return lhs.w * rhs.w + dot(lhs.v, rhs.v);
	}

	quat inverse(const quat& quat) {
		return conjugate(quat) / dot(quat, quat);
	}

	float length(const quat& quat) {
		return sqrt(dot(quat, quat));
	}

	quat normalize(const quat& quat) {
		return quat * (1 / length(quat));
	}

	quat slerp(const quat& from, const quat& to, float weight) {
		// quat q = to / from;
		// float angle = atan2(length(q.v), q.w) * 2;
		// return from * axis_angle(angle * weight, q.v);

		quat q = to / from;

		float c = q.w;
		float s = length(q.v);

		float half_angle = atan2(s, c);
		float inv_sine = 1 / s;

		float k_from = inv_sine * sin(half_angle * (1 - weight));
		float k_to = inv_sine * sin(half_angle * weight);

		return (from * k_from) + (to * k_to);
	}
}
