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

// WorkerSceneInfo describes which mesh primitives a worker is responsible for.
type WorkerSceneInfo struct {
	Work      map[string][]int `json:"work"`       // mesh name -> list of primitive indices
	TotalSize float64          `json:"total_size"`  // estimated size in GB
}

// SplitScene is the result of splitting a GLTF scene across workers.
type SplitScene struct {
	SplitWork map[int]WorkerSceneInfo `json:"split_work"`
	TotalSize float64                 `json:"total_size"`
}

// SubGrid defines the pixel region a worker renders.
type SubGrid struct {
	MinX int `json:"minX"`
	MaxX int `json:"maxX"`
	MinY int `json:"minY"`
	MaxY int `json:"maxY"`
}

// WorkerInfo is the full configuration passed to each Fargate worker task.
type WorkerInfo struct {
	SceneInfo         WorkerSceneInfo `json:"scene_info"`
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

	if len(doc.Scenes) == 0 {
		return nil, fmt.Errorf("scene.gltf has no scenes")
	}

	scene := doc.Scenes[0]

	// Count total primitives
	totalPrimitives := 0
	for _, nodeIdx := range scene.Nodes {
		node := doc.Nodes[nodeIdx]
		if node.Mesh == nil {
			continue
		}
		totalPrimitives += len(doc.Meshes[*node.Mesh].Primitives)
	}

	log.Printf("Scene has %d total primitives, splitting across %d workers", totalPrimitives, numWorkers)

	splitScene := &SplitScene{
		SplitWork: make(map[int]WorkerSceneInfo),
	}

	currentWorkerID := 1
	currentPrimitive := 0
	var totalSize float64

	for _, nodeIdx := range scene.Nodes {
		node := doc.Nodes[nodeIdx]
		if node.Mesh == nil {
			continue
		}

		mesh := doc.Meshes[*node.Mesh]
		meshName := mesh.Name
		if meshName == "" {
			meshName = fmt.Sprintf("mesh_%d", *node.Mesh)
		}

		for primID := range mesh.Primitives {
			currentPrimitive++

			primSize := getPrimitiveSize(ctx, doc, s3Client, bucket, sceneRoot, mesh.Primitives[primID]) * 1e-9 // bytes to GB // Primitives is []*gltf.Primitive
			totalSize += primSize

			worker, ok := splitScene.SplitWork[currentWorkerID]
			if !ok {
				worker = WorkerSceneInfo{Work: make(map[string][]int)}
			}
			worker.Work[meshName] = append(worker.Work[meshName], primID)
			worker.TotalSize += primSize
			splitScene.SplitWork[currentWorkerID] = worker

			// Advance to next worker when this one has its share
			if currentWorkerID < numWorkers && float64(currentPrimitive) >= float64(totalPrimitives)/float64(numWorkers) {
				currentWorkerID++
				currentPrimitive = 0
			}
		}
	}

	splitScene.TotalSize = totalSize
	log.Printf("Scene split complete: total_size=%.4f GB, workers=%d", totalSize, len(splitScene.SplitWork))
	return splitScene, nil
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

	// Master worker: has no scene work, but gets the results queue and full image dimensions
	infos["master"] = WorkerInfo{
		SceneInfo:         WorkerSceneInfo{Work: map[string][]int{}, TotalSize: 0},
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
