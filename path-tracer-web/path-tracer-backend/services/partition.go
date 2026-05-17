package services

import "log"

// SpatialSplit recursively partitions primitives into n groups by
// splitting along the longest axis at the spatial midpoint. Primitives
// whose AABB straddles a split plane are duplicated into both sides —
// this is what keeps intersections correct after the partition.
//
// The returned slice always has exactly n entries (some may be empty
// if the scene is sparse or all primitives clump on one side).
//
// n is treated as the target leaf count. For non-power-of-two n the
// split tree is unbalanced but still produces n leaves.
func SpatialSplit(prims []PrimRef, n int) [][]PrimRef {
	if n <= 0 {
		return nil
	}
	if n == 1 {
		out := make([]PrimRef, len(prims))
		copy(out, prims)
		return [][]PrimRef{out}
	}

	cell := EmptyAABB()
	for _, p := range prims {
		cell = cell.Union(p.AABB)
	}

	leaves := make([][]PrimRef, 0, n)
	if cell.IsEmpty() {
		for i := 0; i < n; i++ {
			leaves = append(leaves, nil)
		}
		return leaves
	}

	recursiveSplit(prims, cell, n, &leaves)

	for len(leaves) < n {
		leaves = append(leaves, nil)
	}
	return leaves
}

// recursiveSplit splits along the longest axis of `cell` at the spatial
// midpoint and recurses until `target == 1`. Primitives crossing the
// split plane appear in both child lists.
func recursiveSplit(prims []PrimRef, cell AABB, target int, out *[][]PrimRef) {
	if target <= 1 {
		// Copy so leaves don't alias the partial slices we built up.
		leaf := make([]PrimRef, len(prims))
		copy(leaf, prims)
		*out = append(*out, leaf)
		return
	}

	axis := cell.LongestAxis()
	splitPos := 0.5 * (cell.Min[axis] + cell.Max[axis])

	var left, right []PrimRef
	for _, p := range prims {
		if p.AABB.Min[axis] < splitPos {
			left = append(left, p)
		}
		if p.AABB.Max[axis] >= splitPos {
			right = append(right, p)
		}
	}

	leftCell := cell
	leftCell.Max[axis] = splitPos
	rightCell := cell
	rightCell.Min[axis] = splitPos

	leftTarget := target / 2
	rightTarget := target - leftTarget

	recursiveSplit(left, leftCell, leftTarget, out)
	recursiveSplit(right, rightCell, rightTarget, out)
}

// LogPartitionSummary prints a one-line summary per leaf — useful while
// you're sanity-checking that the split is doing something reasonable.
func LogPartitionSummary(leaves [][]PrimRef) {
	for i, leaf := range leaves {
		box := EmptyAABB()
		for _, p := range leaf {
			box = box.Union(p.AABB)
		}
		if box.IsEmpty() {
			log.Printf("partition leaf %d: empty", i+1)
			continue
		}
		log.Printf("partition leaf %d: %d primitives, aabb min=%v max=%v",
			i+1, len(leaf), box.Min, box.Max)
	}
}
