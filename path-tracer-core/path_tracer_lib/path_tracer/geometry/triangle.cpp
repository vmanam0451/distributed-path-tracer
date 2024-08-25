#include "path_tracer/geometry/triangle.hpp"

#include "path_tracer/math/mat3.hpp"

using namespace math;

namespace geometry {
	bool triangle::intersection::has_hit() const {
		return distance >= 0;
	}

	triangle::triangle(
		const fvec3& a,
		const fvec3& b,
		const fvec3& c
	) : a(a), b(b), c(c) {
	}

	math::fvec3& triangle::operator[](size_t index) {
		switch (index) {
		case 0:
			return a;
		case 1:
			return b;
		case 2:
			return c;
		default:
			throw std::out_of_range("Triangle index out of range.");
		}
	}

	const math::fvec3& triangle::operator[](size_t index) const {
		switch (index) {
		case 0:
			return a;
		case 1:
			return b;
		case 2:
			return c;
		default:
			throw std::out_of_range("Triangle index out of range.");
		}
	}

	// Geometric solution
	// https://youtu.be/EZXz-uPyCyA

	// triangle::intersection triangle::intersect(const ray &ray) const {
	// 	fvec3 ab = b - a;
	// 	fvec3 bc = c - b;
	// 	fvec3 ca = a - c;
	// 	float denom;

	// 	// We cross two vectors which lay on the triangle's plane
	// 	// Could use -ca for the actual CCW-front normal
	// 	fvec3 normal = cross(ab, ca);

	// 	// If ray is perpendicular to the normal...
	// 	denom = dot(normal, ray.get_dir());
	// 	if (denom == 0)
	// 		return { -1 };

	// 	// d = proj(N, A - O) / proj(N, D)
	// 	// ...where A is any point on the plane
	// 	float dist = dot(normal, a - ray.origin) / denom;

	// 	// If plane is behind the ray
	// 	if (dist < 0)
	// 		return { -1 };

	// 	// This intersection point lies
	// 	// on the same plane as our triangle
	// 	// I = O + D * d
	// 	fvec3 hit = ray.origin + ray.get_dir() * dist;

	// 	// We need to find the barycentric
	// 	// coordinates for that point
	// 	// [alpha, beta, gamma]

	// 	// Triangle heights for vertices A and B
	// 	fvec3 ha = ab - proj(bc, ab);
	// 	fvec3 hb = bc - proj(ca, bc);

	// 	// Alpha
	// 	// alpha = 1 - (proj(ha, AI) / proj(ha, AB))
	// 	// proj(ha, AB) = proj(ha, AC)
	// 	// ha and AB/AC can only be perpendicular
	// 	// for degenerate triangles
	// 	denom = dot(ha, ab);
	// 	if (denom == 0)
	// 		return { -1 };
	// 	float alpha = 1 - (dot(ha, hit - a) / denom);

	// 	// If any of the coordinates is negative,
	// 	// intersection occured outside of the triangle
	// 	if (alpha < 0)
	// 		return { -1 };

	// 	// Beta
	// 	denom = dot(hb, bc);
	// 	if (denom == 0)
	// 		return { -1 };
	// 	float beta = 1 - (dot(hb, hit - b) / denom);

	// 	if (beta < 0)
	// 		return { -1 };

	// 	// Gamma
	// 	float gamma = 1 - (alpha + beta);

	// 	if (gamma < 0)
	// 		return { -1 };

	// 	return { dist, fvec3(alpha, beta, gamma) };
	// }

	// Algebraic solution
	// https://graphicscompendium.com/raytracing/09-triangles

	triangle::intersection triangle::intersect(const ray& ray) const {
		// m * [β, γ, t] = v
		// fmat3 m(
		// 	a - b,
		// 	a - c,
		// 	ray.get_dir()
		// );
		// fvec3 v = a - ray.origin;
		// fvec3 w =  inverse(m) * v;
		// fvec3 b = fvec3(1 - w.x - w.y, w.x, w.y);
		// if (min(b.x, b.y, b.z, w.z) < 0)
		// 	return { -1 };
		// else
		// 	return { w.z, b };

		// m * [β, γ, t] = v
		fmat3 m(
			a - b,
			a - c,
			ray.get_dir()
		);
		fvec3 v = a - ray.origin;

		// Common subterms for the determinants
		float c1 = m.y.y * m.z.z - m.z.y * m.y.z;
		float c2 = m.x.y * m.z.z - m.z.y * m.x.z;
		float c3 = m.x.y * m.y.z - m.y.y * m.x.z;

		float c4 = v.y * m.z.z - m.z.y * v.z;
		float c5 = m.x.y * v.z - v.y * m.x.z;

		float c6 = m.y.y * v.z - v.y * m.y.z;

		// Solve from Cramer's rule
		float inv_det = 1 / (
			m.x.x * c1 -
			m.y.x * c2 +
			m.z.x * c3
		);

		float beta = inv_det * (
			v.x * c1 -
			m.y.x * c4 -
			m.z.x * c6
		);

		if (beta < 0 - epsilon || beta > 1 + epsilon)
			return {-1};

		float gamma = inv_det * (
			m.x.x * c4 -
			v.x * c2 +
			m.z.x * c5
		);

		// Bias in favour of a successful hit
		if (gamma < 0 - epsilon || gamma + beta > 1 + epsilon)
			return {-1};

		float dist = inv_det * (
			m.x.x * c6 -
			m.y.x * c5 +
			v.x * c3
		);

		float alpha = 1 - beta - gamma;

		// Distance is negative if
		// hit occurs behind the ray
		return {dist, fvec3(alpha, beta, gamma)};
	}
}
