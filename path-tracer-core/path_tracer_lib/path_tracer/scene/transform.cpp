#include "path_tracer/scene/transform.hpp"

#include "path_tracer/util/string.hpp"

using namespace math;

namespace scene {
	const transform transform::identity;

	transform::transform(const fvec3& origin, const fmat3& basis)
		: origin(origin), basis(basis) {
	}

	transform transform::make(
		const fvec3& translation,
		const quat& rotation,
		const fvec3& scale) {
		return transform(translation, make_basis(rotation, scale));
	}

	fmat3 transform::make_basis(
		const quat& rotation,
		const fvec3& scale) {
		fmat3 basis = rotation.to_basis();

		basis.x *= scale.x;
		basis.y *= scale.y;
		basis.z *= scale.z;

		return basis;
	}

	transform transform::inverse() const {
		fmat3 basis = math::inverse(this->basis);
		return transform(basis * -origin, basis);
	}

	quat transform::get_rotation() const {
		return quat::from_basis(orthonormalize(basis));
	}

	void transform::set_rotation(const quat& rotation) {
		fvec3 scale = get_scale();

		basis = rotation.to_basis();

		basis.x *= scale.x;
		basis.y *= scale.y;
		basis.z *= scale.z;
	}

	void transform::rotate(const quat& rotation) {
		basis.x = rotation * basis.x;
		basis.y = rotation * basis.y;
		basis.z = rotation * basis.z;
	}

	fvec3 transform::get_scale() const {
		return fvec3(
			length(basis.x),
			length(basis.y),
			length(basis.z)
		);
	}

	void transform::set_scale(const fvec3& scale) {
		basis.x = normalize(basis.x) * scale.x;
		basis.y = normalize(basis.y) * scale.y;
		basis.z = normalize(basis.z) * scale.z;
	}

	void transform::scale(const fvec3& scale) {
		basis.x *= scale.x;
		basis.y *= scale.y;
		basis.z *= scale.z;
	}

	std::ostream& operator<<(std::ostream& lhs, const transform& rhs) {
		std::stringstream lines[3], final_ss;

		for (size_t i = 0; i < 4; i++) {
			size_t max_len = 0;
			for (size_t j = 0; j < 3; j++) {
				float value = (i == 3) ? rhs.origin[j] : rhs.basis[i][j];
				size_t len = util::to_string(value).size();
				if (len > max_len)
					max_len = len;
			}

			for (size_t j = 0; j < 3; j++) {
				float value = (i == 3) ? rhs.origin[j] : rhs.basis[i][j];
				lines[j] << std::setw(max_len) << util::to_string(value);
				if (i != 3)
					lines[j] << " | ";
			}
		}

		for (size_t i = 0; i < 3; i++) {
			if (i != 2) lines[i] << '\n';
			final_ss << lines[i].str();
		}

		return lhs << final_ss.str();
	}

	transform& transform::operator*=(const transform& rhs) {
		return *this = *this * rhs;
	}

	transform transform::operator*(const transform& rhs) const {
		fvec3 origin = this->basis * rhs.origin + this->origin;
		fmat3 basis = this->basis * rhs.basis;

		return transform(origin, basis);
	}

	fvec3 transform::operator*(const fvec3& rhs) const {
		return basis * rhs + origin;
	}
}
