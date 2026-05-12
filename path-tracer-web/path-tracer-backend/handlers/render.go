package handlers

import (
	"context"
	"log"
	"os"
	"pathtracerbackend/services"
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

func cleanupResources(awsConfig aws.Config, queueURL, cloudMapServiceId, ecsClusterArn string, taskArns []string) {
	ctx := context.Background()
	log.Println("Running render cleanup...")

	if queueURL != "" {
		services.DeleteSQSQueue(ctx, queueURL, awsConfig)
	}
	if cloudMapServiceId != "" {
		services.DeleteCloudMapService(ctx, cloudMapServiceId, awsConfig)
	}
	if ecsClusterArn != "" {
		services.StopTasks(ctx, ecsClusterArn, taskArns, awsConfig)
	}

	log.Println("Render cleanup complete")
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

	var queueURL string
	var cloudMapServiceId string
	var taskArns []string

	defer func() {
		cleanupResources(awsConfig, queueURL, cloudMapServiceId, ecsClusterArn, taskArns)
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

	sendStatus("Creating SQS queue...")
	queueName := "render-results-" + renderID
	queueURL, err = services.CreateSQSQueue(setupCtx, queueName, awsConfig)
	if err != nil {
		sendError("Failed to create SQS queue")
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
		ResultsQueueURL:   queueURL,
		AWSRegion:         awsRegion,
	})

	taskArns, err = services.LaunchWorkerTasks(setupCtx, workerInfos, awsConfig)
	if err != nil {
		sendError("Failed to launch worker tasks: " + err.Error())
		return
	}

	sendStatus("Rendering...")
	err = services.PollSQSQueue(setupCtx, queueURL, awsConfig, func(messages []string) {
		// Each SQS message body is already a JSON array of pixels: "[{p1},{p2},...]"
		for _, msg := range messages {
			c.SSEvent("renderUpdate", gin.H{"message": msg})
		}
		c.Writer.Flush()
	}, func() {
		c.SSEvent("keepalive", "")
		c.Writer.Flush()
	})

	if err != nil {
		log.Printf("Polling ended: %v", err)
	}

	c.SSEvent("done", gin.H{"message": "Render complete"})
	c.Writer.Flush()
}