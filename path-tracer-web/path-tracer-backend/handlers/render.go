package handlers

import (
	"context"
	"log"
	"os"
	"pathtracerbackend/services"
	"strconv"
	"time"

	"github.com/aws/aws-sdk-go-v2/aws"
	"github.com/aws/aws-sdk-go-v2/config"
	"github.com/gin-gonic/gin"
	"github.com/google/uuid"
)

type renderRequest struct {
	SceneBucket string `json:"sceneBucket" binding:"required"`
	SceneKey    string `json:"sceneKey" binding:"required"`
	SceneName   string `json:"sceneName" binding:"required"`
	NumWorkers  int    `json:"numWorkers" binding:"required"`
	NumSamples  int    `json:"numSamples" binding:"required"`
	NumBounces  int    `json:"numBounces" binding:"required"`
	X           int    `json:"X" binding:"required"`
	Y           int    `json:"Y" binding:"required"`
}

func cleanupResources(awsConfig aws.Config, cloudMapServiceId, ecsClusterArn string, taskArns []string, listener *services.TCPPeerListener) {
	ctx := context.Background()
	log.Println("Running render cleanup...")

	if listener != nil {
		listener.Stop()
	}
	if ecsClusterArn != "" {
		services.StopTasks(ctx, ecsClusterArn, taskArns, awsConfig)
	}
	if cloudMapServiceId != "" {
		services.DeleteCloudMapService(ctx, cloudMapServiceId, awsConfig)
	}

	log.Println("Render cleanup complete")
}

// pickWebTCPPort decides which port the per-render TCP listener binds to.
// WEB_TCP_PORT can be set explicitly (typically when the container exposes
// a known port to the VPC); otherwise we let the OS choose ephemerally,
// which is fine for local dev.
func pickWebTCPPort() int {
	if v := os.Getenv("WEB_TCP_PORT"); v != "" {
		if p, err := strconv.Atoi(v); err == nil {
			return p
		}
	}
	return 0
}

// webAdvertisedHost is the address the master will dial. In Fargate this is
// the container's private IP / service-discovery name; locally it's whatever
// the workers can reach.
func webAdvertisedHost() string {
	if v := os.Getenv("WEB_ADVERTISED_HOST"); v != "" {
		return v
	}
	return "127.0.0.1"
}

func Render(c *gin.Context) {
	var req renderRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(400, gin.H{"error": err.Error()})
		return
	}

	awsConfig, err := config.LoadDefaultConfig(context.TODO())
	if err != nil {
		c.JSON(500, gin.H{"error": "Failed to load AWS config"})
		return
	}

	namespaceId := os.Getenv("CLOUD_MAP_NAMESPACE_ID")
	namespaceName := os.Getenv("CLOUD_MAP_NAMESPACE_NAME")
	ecsClusterArn := os.Getenv("ECS_CLUSTER_ARN")
	awsRegion := os.Getenv("AWS_REGION")
	if awsRegion == "" {
		awsRegion = os.Getenv("AWS_DEFAULT_REGION")
	}

	renderID := uuid.New().String()[:8]
	serviceName := req.SceneName + "-" + renderID

	var cloudMapServiceId string
	var taskArns []string
	var listener *services.TCPPeerListener

	defer func() {
		cleanupResources(awsConfig, cloudMapServiceId, ecsClusterArn, taskArns, listener)
	}()

	c.Header("Content-Type", "text/event-stream")
	c.Header("Cache-Control", "no-cache")
	c.Header("Connection", "keep-alive")

	sendStatus := func(status string) {
		c.SSEvent("status", gin.H{"message": status})
		c.Writer.Flush()
	}

	sendError := func(msg string) {
		c.SSEvent("error", gin.H{"message": msg})
		c.Writer.Flush()
	}

	setupCtx := context.Background()

	sendStatus("Creating Cloud Map service...")
	cloudMapServiceId, err = services.CreateCloudMapService(setupCtx, namespaceId, serviceName, awsConfig)
	if err != nil {
		sendError("Failed to create Cloud Map service")
		return
	}

	// Start the TCP listener BEFORE launching workers so the master can dial
	// in as soon as it boots. The master retries with backoff if we're slow,
	// but this avoids the spurious reconnect storm on slow starts.
	sendStatus("Starting pixel listener...")
	pixelBatches := make(chan string, 256)
	terminated := make(chan struct{})
	listener = services.NewTCPPeerListener(
		pickWebTCPPort(),
		func(body string) {
			// Non-blocking: drop the batch if the client is dawdling. The
			// channel is large enough that this should never fire in practice.
			select {
			case pixelBatches <- body:
			default:
				log.Printf("Pixel batch channel full; dropping batch (%d bytes)", len(body))
			}
		},
		func() {
			close(terminated)
		},
	)

	boundPort, err := listener.Start(setupCtx)
	if err != nil {
		sendError("Failed to start TCP listener: " + err.Error())
		return
	}

	sendStatus("Preprocessing scene...")
	splitScene, err := services.PreprocessScene(setupCtx, awsConfig, req.SceneBucket, req.SceneKey, req.NumWorkers)
	if err != nil {
		sendError("Failed to preprocess scene: " + err.Error())
		return
	}

	sendStatus("Launching worker tasks...")
	workerInfos := services.BuildWorkerInfos(splitScene, services.WorkerInfoParams{
		SceneBucket:       req.SceneBucket,
		SceneKey:          req.SceneKey,
		NumWorkers:        req.NumWorkers,
		Samples:           req.NumSamples,
		Bounces:           req.NumBounces,
		ImageWidth:        req.X,
		ImageHeight:       req.Y,
		CloudMapNamespace: namespaceName,
		CloudMapService:   serviceName,
		CloudMapServiceId: cloudMapServiceId,
		AWSRegion:         awsRegion,
		WebHost:           webAdvertisedHost(),
		WebPort:           boundPort,
	})

	taskArns, err = services.LaunchWorkerTasks(setupCtx, workerInfos, awsConfig)
	if err != nil {
		sendError("Failed to launch worker tasks: " + err.Error())
		return
	}

	sendStatus("Rendering...")

	// Stream pixel batches over SSE until the master signals termination,
	// the inbound TCP connection drops, or the client disconnects.
	keepaliveTicker := time.NewTicker(1 * time.Second)
	defer keepaliveTicker.Stop()

streamLoop:
	for {
		select {
		case <-c.Request.Context().Done():
			log.Printf("Client disconnected, stopping render stream")
			break streamLoop
		case batch := <-pixelBatches:
			c.SSEvent("renderUpdate", gin.H{"message": batch})
			c.Writer.Flush()
		case <-keepaliveTicker.C:
			c.SSEvent("keepalive", "")
			c.Writer.Flush()
		case <-terminated:
			// Drain anything still buffered so the final frame isn't lost.
			for {
				select {
				case batch := <-pixelBatches:
					c.SSEvent("renderUpdate", gin.H{"message": batch})
					c.Writer.Flush()
				default:
					break streamLoop
				}
			}
		}
	}

	c.SSEvent("done", gin.H{"message": "Render complete"})
	c.Writer.Flush()
}
