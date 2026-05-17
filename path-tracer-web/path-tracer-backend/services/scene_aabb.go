package services

import (
	"fmt"

	"github.com/qmuntal/gltf"
)

// PrimRef identifies one *instance* of a primitive in the scene. A given
// (mesh, primitive) pair can appear multiple times in a glTF document
// when the same mesh is referenced by multiple nodes — each one becomes
// its own PrimRef so the spatial partitioner can route them to
// different workers independently.
//
// WorldMatrix is a 4x4 column-major glTF-convention transform that
// places the primitive at its final scene position. AABB is the
// world-space bounding box (POSITION min/max transformed by WorldMatrix).
type PrimRef struct {
	MeshName    string
	PrimIdx     int
	WorldMatrix [16]float64
	AABB        AABB
}

// CollectPrimitives walks the first scene of the document, applies node
// transforms as it descends the hierarchy, and returns one PrimRef per
// (node, primitive) instance.
//
// Unlike a per-(mesh, prim) collapse, this preserves spatial identity:
// two instances of the same mesh on opposite ends of the scene end up
// as two separate entries with two separate world-space AABBs, so the
// spatial split can put them on different workers without inflating
// either worker's AABB.
func CollectPrimitives(doc *gltf.Document) ([]PrimRef, error) {
	if len(doc.Scenes) == 0 {
		return nil, fmt.Errorf("scene.gltf has no scenes")
	}
	scene := doc.Scenes[0]

	var out []PrimRef

	var walk func(nodeIdx int, parent [16]float64)
	walk = func(nodeIdx int, parent [16]float64) {
		if nodeIdx < 0 || nodeIdx >= len(doc.Nodes) {
			return
		}
		node := doc.Nodes[nodeIdx]
		world := MatMul(parent, nodeLocalMatrix(node))

		if node.Mesh != nil {
			mesh := doc.Meshes[*node.Mesh]
			meshName := mesh.Name
			if meshName == "" {
				meshName = fmt.Sprintf("mesh_%d", *node.Mesh)
			}
			for primIdx, prim := range mesh.Primitives {
				worldAABB, ok := primitiveWorldAABB(doc, prim, world)
				if !ok {
					continue
				}
				out = append(out, PrimRef{
					MeshName:    meshName,
					PrimIdx:     primIdx,
					WorldMatrix: world,
					AABB:        worldAABB,
				})
			}
		}

		for _, child := range node.Children {
			walk(child, world)
		}
	}

	for _, root := range scene.Nodes {
		walk(root, Identity4)
	}

	return out, nil
}

// nodeLocalMatrix returns the node's local-to-parent transform.
// A node defines its transform with either an explicit Matrix or a
// TRS triple, never both. The qmuntal/gltf library leaves Matrix as
// the zero matrix when it's unset, so that's our discriminator.
func nodeLocalMatrix(n *gltf.Node) [16]float64 {
	var zero [16]float64
	if n.Matrix != zero {
		return n.Matrix
	}
	return ComposeTRS(n.TranslationOrDefault(), n.RotationOrDefault(), n.ScaleOrDefault())
}

// primitiveWorldAABB returns the world-space AABB of a primitive.
//
// glTF requires the POSITION accessor to populate Min/Max, so we read
// those directly and transform the resulting box. This avoids decoding
// vertex buffers from S3 just to compute bounds.
//
// Returns (zero, false) if POSITION is missing or its bounds are
// malformed — the caller should treat such primitives as unassignable.
func primitiveWorldAABB(doc *gltf.Document, prim *gltf.Primitive, world [16]float64) (AABB, bool) {
	posIdx, ok := prim.Attributes["POSITION"]
	if !ok {
		return AABB{}, false
	}
	idx := int(posIdx)
	if idx < 0 || idx >= len(doc.Accessors) {
		return AABB{}, false
	}
	acc := doc.Accessors[idx]
	if len(acc.Min) < 3 || len(acc.Max) < 3 {
		return AABB{}, false
	}
	local := AABB{
		Min: Vec3{acc.Min[0], acc.Min[1], acc.Min[2]},
		Max: Vec3{acc.Max[0], acc.Max[1], acc.Max[2]},
	}
	return TransformAABB(world, local), true
}
