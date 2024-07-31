namespace math {
	template <typename Epsilon>
	bool is_approx(const quat& a, const quat& b, Epsilon epsilon) {
		return is_approx(a.w, b.w, epsilon) &&
			is_approx(a.v, b.v, epsilon);
	}

	template <scalar Epsilon>
	bool is_normalized(const quat& quat, Epsilon epsilon) {
		return is_approx(dot(quat, quat), 1, epsilon);
	}
}
