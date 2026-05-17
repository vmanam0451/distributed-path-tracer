package services

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"github.com/aws/aws-sdk-go-v2/aws"
	"github.com/aws/aws-sdk-go-v2/service/s3"
	"github.com/qmuntal/gltf"
)

// SceneInstance is one placement of a glTF primitive in the world. The
// preprocessor pre-bakes the world-space transform so the worker
// doesn't have to walk the node graph to find it.
//
// WorldMatrix is column-major, glTF-convention.
type SceneInstance struct {
	MeshName    string      `json:"mesh_name"`
	PrimIdx     int         `json:"prim_idx"`
	WorldMatrix [16]float64 `json:"world_matrix"`
}

// WorkerSceneInfo describes which primitive *instances* a worker is
// responsible for. Each entry is independent: two instances of the
// same mesh on opposite ends of the scene can be assigned to different
// workers without inflating either worker's AABB.
//
// TotalSize is computed by deduplicating instances that share the same
// (mesh_name, prim_idx) — they share underlying mesh data on the
// worker, so storage cost is counted once per unique primitive.
type WorkerSceneInfo struct {
	Instances []SceneInstance `json:"instances"`
	TotalSize float64         `json:"total_size"`
}

// SplitScene is the result of splitting a GLTF scene across workers.
//
// AABBTable holds one tight world-space AABB per worker, computed from
// the primitives actually assigned to that worker (not from the
// partition cell, which would be looser). Every worker — and the
// master — receives a copy of this table so each worker can locally
// pre-filter intersection requests before sending them on the network.
type SplitScene struct {
	SplitWork map[int]WorkerSceneInfo `json:"split_work"`
	TotalSize float64                 `json:"total_size"`
	AABBTable []AABBEntry             `json:"aabb_table"`
}

// SubGrid defines the pixel region a worker renders.
type SubGrid struct {
	MinX int `json:"minX"`
	MaxX int `json:"maxX"`
	MinY int `json:"minY"`
	MaxY int `json:"maxY"`
}

// WorkerInfo is the full configuration passed to each Fargate worker task.
//
// AABBTable is the replicated worker-id -> world-space-AABB table the
// worker uses to decide which peers a ray could possibly hit before
// fanning out the intersection query.
type WorkerInfo struct {
	SceneInfo         WorkerSceneInfo `json:"scene_info"`
	AABBTable         []AABBEntry     `json:"aabb_table"`
	SceneBucket       string          `json:"scene_bucket"`
	SceneRoot         string          `json:"scene_root"`
	WorkerID          string          `json:"worker_id"`
	NumWorkers        int             `json:"num_workers"`
	Samples           int             `json:"samples"`
	Bounces           int             `json:"bounces"`
	MinX              int             `json:"min_x"`
	MaxX              int             `json:"max_x"`
	MinY              int             `json:"min_y"`
	MaxY              int             `json:"max_y"`
	ImageWidth        int             `json:"image_width"`
	ImageHeight       int             `json:"image_height"`
	CloudMapNamespace string          `json:"cloud_map_namespace"`
	CloudMapService   string          `json:"cloud_map_service"`
	CloudMapServiceId string          `json:"cloud_map_service_id"`
	ResultsQueueURL   string          `json:"results_queue_url"`
	AWSRegion         string          `json:"aws_region"`
}

// SplitGrid divides the image into horizontal strips, one per worker.
func SplitGrid(width, height, numWorkers int) map[int]SubGrid {
	grids := make(map[int]SubGrid, numWorkers)
	for i := 1; i <= numWorkers; i++ {
		yStart := (height * (i - 1)) / numWorkers
		yEnd := (height*i)/numWorkers - 1
		grids[i] = SubGrid{
			MinX: 0,
			MaxX: width - 1,
			MinY: yStart,
			MaxY: yEnd,
		}
	}
	return grids
}

// getBufferViewSize safely returns the byte length of a buffer view for the given accessor index.
func getBufferViewSize(doc *gltf.Document, accessorIndex int) float64 {
	if accessorIndex < 0 || accessorIndex >= len(doc.Accessors) {
		return 0
	}
	acc := doc.Accessors[accessorIndex]
	if acc.BufferView == nil {
		return 0
	}
	bv := doc.BufferViews[*acc.BufferView]
	return float64(bv.ByteLength)
}

// getTextureFileSize returns the S3 object size of a texture's image URI.
func getTextureFileSize(ctx context.Context, doc *gltf.Document, s3Client *s3.Client, bucket, sceneRoot string, texIndex int) float64 {
	if texIndex < 0 || texIndex >= len(doc.Textures) {
		return 0
	}
	tex := doc.Textures[texIndex]
	if tex.Source == nil {
		return 0
	}
	img := doc.Images[*tex.Source]
	if img.URI == "" {
		return 0
	}
	key := sceneRoot + img.URI
	head, err := s3Client.HeadObject(ctx, &s3.HeadObjectInput{
		Bucket: aws.String(bucket),
		Key:    aws.String(key),
	})
	if err != nil {
		log.Printf("Warning: could not HEAD s3://%s/%s: %v", bucket, key, err)
		return 0
	}
	if head.ContentLength != nil {
		return float64(*head.ContentLength)
	}
	return 0
}

// getPrimitiveSize estimates the memory footprint of a single primitive (buffers + textures).
func getPrimitiveSize(ctx context.Context, doc *gltf.Document, s3Client *s3.Client, bucket, sceneRoot string, prim *gltf.Primitive) float64 {
	var bufferSize float64
	for _, idx := range prim.Attributes {
		bufferSize += getBufferViewSize(doc, int(idx))
	}

	var materialSize float64
	if prim.Material != nil {
		mat := doc.Materials[*prim.Material]
		if mat.NormalTexture != nil && mat.NormalTexture.Index != nil {
			materialSize += getTextureFileSize(ctx, doc, s3Client, bucket, sceneRoot, *mat.NormalTexture.Index)
		}
		if mat.OcclusionTexture != nil && mat.OcclusionTexture.Index != nil {
			materialSize += getTextureFileSize(ctx, doc, s3Client, bucket, sceneRoot, *mat.OcclusionTexture.Index)
		}
		if mat.EmissiveTexture != nil {
			materialSize += getTextureFileSize(ctx, doc, s3Client, bucket, sceneRoot, mat.EmissiveTexture.Index)
		}
		if mat.PBRMetallicRoughness != nil {
			if mat.PBRMetallicRoughness.BaseColorTexture != nil {
				materialSize += getTextureFileSize(ctx, doc, s3Client, bucket, sceneRoot, mat.PBRMetallicRoughness.BaseColorTexture.Index)
			}
			if mat.PBRMetallicRoughness.MetallicRoughnessTexture != nil {
				materialSize += getTextureFileSize(ctx, doc, s3Client, bucket, sceneRoot, mat.PBRMetallicRoughness.MetallicRoughnessTexture.Index)
			}
		}
	}

	return bufferSize + materialSize
}

// PreprocessScene downloads a GLTF scene from S3, parses it, and splits its
// primitives across the given number of workers.
func PreprocessScene(ctx context.Context, cfg aws.Config, bucket, sceneRoot string, numWorkers int) (*SplitScene, error) {
	s3Client := s3.NewFromConfig(cfg)

	key := sceneRoot + "scene.gltf"
	log.Printf("Downloading scene from s3://%s/%s", bucket, key)

	resp, err := s3Client.GetObject(ctx, &s3.GetObjectInput{
		Bucket: aws.String(bucket),
		Key:    aws.String(key),
	})
	if err != nil {
		return nil, fmt.Errorf("failed to download scene.gltf: %w", err)
	}
	defer resp.Body.Close()

	data, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, fmt.Errorf("failed to read scene.gltf: %w", err)
	}

	doc := new(gltf.Document)
	if err := json.Unmarshal(data, doc); err != nil {
		return nil, fmt.Errorf("failed to parse scene.gltf: %w", err)
	}

	// Walk the scene graph applying node transforms to get a flat list
	// of primitives with world-space AABBs. These are the units the
	// spatial partitioner operates on.
	prims, err := CollectPrimitives(doc)
	if err != nil {
		return nil, err
	}
	log.Printf("Scene has %d unique primitives, splitting across %d workers", len(prims), numWorkers)

	// Pre-compute per-primitive storage size once, keyed by (mesh, prim).
	// A primitive that gets duplicated across split planes will be sent
	// to multiple workers, and each worker's TotalSize must account for
	// the copy it stores — but we don't want to pay the S3 HEAD cost
	// more than once per primitive.
	sizes := map[primSizeKey]float64{}
	var totalSize float64
	for _, root := range doc.Scenes[0].Nodes {
		accumulatePrimitiveSizes(ctx, doc, s3Client, bucket, sceneRoot, root, sizes, &totalSize)
	}

	// Spatial split: median-on-longest-axis recursion with duplication
	// for primitives that straddle a split plane.
	leaves := SpatialSplit(prims, numWorkers)
	LogPartitionSummary(leaves)

	splitScene := &SplitScene{
		SplitWork: make(map[int]WorkerSceneInfo, numWorkers),
		TotalSize: totalSize,
		AABBTable: make([]AABBEntry, 0, numWorkers),
	}

	for i, leaf := range leaves {
		workerID := i + 1
		info := WorkerSceneInfo{Instances: make([]SceneInstance, 0, len(leaf))}
		leafBox := EmptyAABB()

		// Track unique (mesh, prim) pairs to dedupe size. Two instances
		// of the same primitive at the same worker share loaded mesh
		// data, so storage cost is counted once per unique primitive.
		uniquePrims := map[primSizeKey]struct{}{}

		for _, p := range leaf {
			info.Instances = append(info.Instances, SceneInstance{
				MeshName:    p.MeshName,
				PrimIdx:     p.PrimIdx,
				WorldMatrix: p.WorldMatrix,
			})
			leafBox = leafBox.Union(p.AABB)

			k := primSizeKey{p.MeshName, p.PrimIdx}
			if _, seen := uniquePrims[k]; !seen {
				uniquePrims[k] = struct{}{}
				info.TotalSize += sizes[k]
			}
		}

		splitScene.SplitWork[workerID] = info
		// EmptyAABB uses +/-inf which can't be marshaled to JSON.
		// Substitute a finite degenerate box; the worker treats any
		// min > max box as a guaranteed ray-miss in the pre-filter.
		publishBox := leafBox
		if publishBox.IsEmpty() {
			publishBox = FiniteEmptyAABB()
		}
		splitScene.AABBTable = append(splitScene.AABBTable, AABBEntry{
			WorkerID: fmt.Sprintf("%d", workerID),
			AABB:     publishBox,
		})
	}

	log.Printf("Scene split complete: total_size=%.4f GB, workers=%d", totalSize, len(splitScene.SplitWork))
	return splitScene, nil
}

// primSizeKey is the lookup key for the per-primitive size cache. Each
// (mesh, primitive) pair is sized exactly once even if it ends up
// duplicated across multiple worker leaves.
type primSizeKey struct {
	meshName string
	primIdx  int
}

// accumulatePrimitiveSizes walks the scene graph recursively (so it
// matches CollectPrimitives' traversal) and records the storage size
// for each unique (mesh, primitive) pair. Duplicate sightings — same
// mesh referenced by multiple nodes — are counted once.
func accumulatePrimitiveSizes(
	ctx context.Context,
	doc *gltf.Document,
	s3Client *s3.Client,
	bucket, sceneRoot string,
	nodeIdx int,
	sizes map[primSizeKey]float64,
	totalSize *float64,
) {
	if nodeIdx < 0 || nodeIdx >= len(doc.Nodes) {
		return
	}
	node := doc.Nodes[nodeIdx]
	if node.Mesh != nil {
		mesh := doc.Meshes[*node.Mesh]
		meshName := mesh.Name
		if meshName == "" {
			meshName = fmt.Sprintf("mesh_%d", *node.Mesh)
		}
		for primIdx, prim := range mesh.Primitives {
			k := primSizeKey{meshName, primIdx}
			if _, seen := sizes[k]; seen {
				continue
			}
			size := getPrimitiveSize(ctx, doc, s3Client, bucket, sceneRoot, prim) * 1e-9 // bytes -> GB
			sizes[k] = size
			*totalSize += size
		}
	}
	for _, child := range node.Children {
		accumulatePrimitiveSizes(ctx, doc, s3Client, bucket, sceneRoot, child, sizes, totalSize)
	}
}

// BuildWorkerInfos constructs the full WorkerInfo for each worker + the master.
func BuildWorkerInfos(
	splitScene *SplitScene,
	req WorkerInfoParams,
) map[string]WorkerInfo {
	subGrid := SplitGrid(req.ImageWidth, req.ImageHeight, req.NumWorkers)
	infos := make(map[string]WorkerInfo, len(splitScene.SplitWork)+1)

	for workerID, sceneInfo := range splitScene.SplitWork {
		grid := subGrid[workerID]
		infos[fmt.Sprintf("%d", workerID)] = WorkerInfo{
			SceneInfo:         sceneInfo,
			AABBTable:         splitScene.AABBTable,
			SceneBucket:       req.SceneBucket,
			SceneRoot:         req.SceneKey,
			WorkerID:          fmt.Sprintf("%d", workerID),
			NumWorkers:        len(splitScene.SplitWork),
			Samples:           req.Samples,
			Bounces:           req.Bounces,
			MinX:              grid.MinX,
			MaxX:              grid.MaxX,
			MinY:              grid.MinY,
			MaxY:              grid.MaxY,
			ImageWidth:        req.ImageWidth,
			ImageHeight:       req.ImageHeight,
			CloudMapNamespace: req.CloudMapNamespace,
			CloudMapService:   req.CloudMapService,
			CloudMapServiceId: req.CloudMapServiceId,
			ResultsQueueURL:   "",
			AWSRegion:         req.AWSRegion,
		}
	}

	// Master worker: owns no geometry, but still receives the AABB
	// table so it can route or log intersection traffic if needed.
	infos["master"] = WorkerInfo{
		// Initialize Instances as an empty slice (not nil) so it
		// serializes to [] rather than null — nlohmann's get_to
		// rejects null for std::vector.
		SceneInfo:         WorkerSceneInfo{Instances: []SceneInstance{}, TotalSize: 0},
		AABBTable:         splitScene.AABBTable,
		SceneBucket:       req.SceneBucket,
		SceneRoot:         req.SceneKey,
		WorkerID:          "MASTER",
		NumWorkers:        len(splitScene.SplitWork),
		Samples:           req.Samples,
		Bounces:           req.Bounces,
		MinX:              0,
		MaxX:              req.ImageWidth,
		MinY:              0,
		MaxY:              req.ImageHeight,
		ImageWidth:        req.ImageWidth,
		ImageHeight:       req.ImageHeight,
		CloudMapNamespace: req.CloudMapNamespace,
		CloudMapService:   req.CloudMapService,
		CloudMapServiceId: req.CloudMapServiceId,
		ResultsQueueURL:   req.ResultsQueueURL,
		AWSRegion:         req.AWSRegion,
	}

	return infos
}

// WorkerInfoParams groups the parameters needed to build worker infos.
type WorkerInfoParams struct {
	SceneBucket       string
	SceneKey          string
	NumWorkers        int
	Samples           int
	Bounces           int
	ImageWidth        int
	ImageHeight       int
	CloudMapNamespace string
	CloudMapService   string
	CloudMapServiceId string
	ResultsQueueURL   string
	AWSRegion         string
}