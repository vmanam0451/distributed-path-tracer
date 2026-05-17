package services

import "math"

// Vec3 is a 3D vector / point in world space.
type Vec3 [3]float64

// AABB is an axis-aligned bounding box.
//
// The convention is that an "empty" AABB has Min = +inf and Max = -inf,
// which is the identity element for Union — handy when building bounds
// by folding over a slice of primitives.
type AABB struct {
	Min Vec3 `json:"min"`
	Max Vec3 `json:"max"`
}

// AABBEntry pairs an AABB with the worker that owns the geometry inside
// it. The preprocessor builds one of these per worker and replicates
// the full table to every worker so they can locally pre-filter rays.
type AABBEntry struct {
	WorkerID string `json:"worker_id"`
	AABB     AABB   `json:"aabb"`
}

// EmptyAABB returns an AABB that contains no points. Useful as the
// starting value when computing the union over a list of primitives.
//
// Note: the +/-inf sentinel is NOT JSON-marshalable. Use
// FiniteEmptyAABB when you need a serializable "empty" box (e.g. for
// a worker with no assigned geometry).
func EmptyAABB() AABB {
	return AABB{
		Min: Vec3{math.Inf(1), math.Inf(1), math.Inf(1)},
		Max: Vec3{math.Inf(-1), math.Inf(-1), math.Inf(-1)},
	}
}

// FiniteEmptyAABB returns an AABB that is degenerate (min > max on
// every axis) but uses finite values so it survives JSON marshaling.
// The C++ ray-AABB tester treats any min > max box as a guaranteed
// miss, so this is safe to put in the AABB table for a worker that
// happens to own no primitives.
func FiniteEmptyAABB() AABB {
	return AABB{
		Min: Vec3{1, 1, 1},
		Max: Vec3{-1, -1, -1},
	}
}

// IsEmpty reports whether the AABB contains no points.
func (b AABB) IsEmpty() bool {
	return b.Min[0] > b.Max[0] || b.Min[1] > b.Max[1] || b.Min[2] > b.Max[2]
}

// Union returns the smallest AABB enclosing both b and o.
func (b AABB) Union(o AABB) AABB {
	out := AABB{}
	for i := 0; i < 3; i++ {
		out.Min[i] = math.Min(b.Min[i], o.Min[i])
		out.Max[i] = math.Max(b.Max[i], o.Max[i])
	}
	return out
}

// ExpandPoint returns an AABB that includes both b and p.
func (b AABB) ExpandPoint(p Vec3) AABB {
	out := b
	for i := 0; i < 3; i++ {
		if p[i] < out.Min[i] {
			out.Min[i] = p[i]
		}
		if p[i] > out.Max[i] {
			out.Max[i] = p[i]
		}
	}
	return out
}

// Centroid returns the geometric centre of the box.
func (b AABB) Centroid() Vec3 {
	return Vec3{
		0.5 * (b.Min[0] + b.Max[0]),
		0.5 * (b.Min[1] + b.Max[1]),
		0.5 * (b.Min[2] + b.Max[2]),
	}
}

// LongestAxis returns 0/1/2 for x/y/z, whichever extent is greatest.
// Ties break toward the lower axis.
func (b AABB) LongestAxis() int {
	ex := b.Max[0] - b.Min[0]
	ey := b.Max[1] - b.Min[1]
	ez := b.Max[2] - b.Min[2]
	switch {
	case ex >= ey && ex >= ez:
		return 0
	case ey >= ez:
		return 1
	default:
		return 2
	}
}

// Identity4 is the 4x4 identity matrix in column-major order.
// Matches the layout used by qmuntal/gltf's Node.Matrix.
var Identity4 = [16]float64{
	1, 0, 0, 0,
	0, 1, 0, 0,
	0, 0, 1, 0,
	0, 0, 0, 1,
}

// MatMul returns a * b where both are 4x4 column-major matrices.
func MatMul(a, b [16]float64) [16]float64 {
	var c [16]float64
	for col := 0; col < 4; col++ {
		for row := 0; row < 4; row++ {
			s := 0.0
			for k := 0; k < 4; k++ {
				s += a[k*4+row] * b[col*4+k]
			}
			c[col*4+row] = s
		}
	}
	return c
}

// TransformPoint applies a 4x4 column-major matrix to a 3D point (w=1).
func TransformPoint(m [16]float64, p Vec3) Vec3 {
	return Vec3{
		m[0]*p[0] + m[4]*p[1] + m[8]*p[2] + m[12],
		m[1]*p[0] + m[5]*p[1] + m[9]*p[2] + m[13],
		m[2]*p[0] + m[6]*p[1] + m[10]*p[2] + m[14],
	}
}

// TransformAABB returns a world-space AABB that conservatively bounds
// the given local-space AABB after transformation. Computed by
// transforming all 8 corners and re-bounding — this is loose under
// rotation but is always correct.
func TransformAABB(m [16]float64, b AABB) AABB {
	out := EmptyAABB()
	for i := 0; i < 8; i++ {
		c := Vec3{b.Min[0], b.Min[1], b.Min[2]}
		if i&1 != 0 {
			c[0] = b.Max[0]
		}
		if i&2 != 0 {
			c[1] = b.Max[1]
		}
		if i&4 != 0 {
			c[2] = b.Max[2]
		}
		out = out.ExpandPoint(TransformPoint(m, c))
	}
	return out
}

// ComposeTRS builds a column-major 4x4 matrix M = T * R * S where R is
// constructed from the unit quaternion q = (qx, qy, qz, qw). Matches
// the glTF 2.0 convention.
func ComposeTRS(t [3]float64, q [4]float64, s [3]float64) [16]float64 {
	qx, qy, qz, qw := q[0], q[1], q[2], q[3]

	xx := qx * qx
	yy := qy * qy
	zz := qz * qz
	xy := qx * qy
	xz := qx * qz
	yz := qy * qz
	wx := qw * qx
	wy := qw * qy
	wz := qw * qz

	// Each row below is one column of the column-major matrix.
	return [16]float64{
		(1 - 2*(yy+zz)) * s[0], (2 * (xy + wz)) * s[0], (2 * (xz - wy)) * s[0], 0,
		(2 * (xy - wz)) * s[1], (1 - 2*(xx+zz)) * s[1], (2 * (yz + wx)) * s[1], 0,
		(2 * (xz + wy)) * s[2], (2 * (yz - wx)) * s[2], (1 - 2*(xx+yy)) * s[2], 0,
		t[0], t[1], t[2], 1,
	}
}
