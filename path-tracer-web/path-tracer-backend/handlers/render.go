package handlers

import (
	"context"
	"log"
	"os"
	"pathtracerbackend/services"

	"github.com/aws/aws-sdk-go-v2/aws"
	"github.com/aws/aws-sdk-go-v2/config"
	"github.com/gin-gonic/gin"
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

// cleanupResources tears down all AWS resources created during a render.
// Uses context.Background() so cleanup succeeds even if the request context is cancelled.
func cleanupResources(awsConfig aws.Config, queueURL, cloudMapServiceId, ecsClusterArn, taskDefinitionArn string) {
	ctx := context.Background()
	log.Println("Running render cleanup...")

	if queueURL != "" {
		services.DeleteSQSQueue(ctx, queueURL, awsConfig)
	}
	if cloudMapServiceId != "" {
		services.DeleteCloudMapService(ctx, cloudMapServiceId, awsConfig)
	}
	if ecsClusterArn != "" && taskDefinitionArn != "" {
		services.StopAllTasks(ctx, ecsClusterArn, taskDefinitionArn, awsConfig)
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

	lambdaArn := os.Getenv("PREPROCESSOR_LAMBDA_ARN")
	namespaceId := os.Getenv("CLOUD_MAP_NAMESPACE_ID")
	namespaceName := os.Getenv("CLOUD_MAP_NAMESPACE_NAME")
	ecsClusterArn := os.Getenv("ECS_CLUSTER_ARN")
	taskDefinitionArn := os.Getenv("TASK_DEFINITION_ARN")
	serviceName := req.SceneName + "-workers"

	// Track created resources for cleanup
	var queueURL string
	var cloudMapServiceId string

	defer func() {
		cleanupResources(awsConfig, queueURL, cloudMapServiceId, ecsClusterArn, taskDefinitionArn)
	}()

	ctx := c.Request.Context()

	cloudMapServiceId, err = services.CreateCloudMapService(ctx, namespaceId, serviceName, awsConfig)
	if err != nil {
		c.JSON(500, gin.H{"error": "Failed to create Cloud Map service"})
		return
	}

	queueName := "render-results-queue" + req.SceneName
	queueURL, err = services.CreateSQSQueue(ctx, queueName, awsConfig)
	if err != nil {
		c.JSON(500, gin.H{"error": "Failed to create SQS queue"})
		return
	}

	lambdaPayload := createLambdaPayload(req, namespaceName, serviceName, cloudMapServiceId, queueURL)
	err = services.InvokePreprocessorLambda(ctx, lambdaArn, lambdaPayload, awsConfig)
	if err != nil {
		c.JSON(500, gin.H{"error": "Failed to invoke Lambda function: " + err.Error()})
		return
	}

	c.Header("Content-Type", "text/event-stream")
	c.Header("Cache-Control", "no-cache")
	c.Header("Connection", "keep-alive")

	err = services.PollSQSQueue(ctx, queueURL, awsConfig, func(message string) {
		c.SSEvent("renderUpdate", gin.H{"message": message})
		c.Writer.Flush()
	})

	if err != nil && ctx.Err() == nil {
		c.JSON(500, gin.H{"error": "Failed to poll SQS queue"})
		return
	}
}

func createLambdaPayload(req renderRequest, namespaceName, serviceName, serviceId, resultsQueueURL string) services.LambdaRequest {
	return services.LambdaRequest{
		SceneBucket: req.SceneBucket,
		SceneKey: req.SceneKey,
		NumWorkers: req.NumWorkers,
		NumSamples: req.NumSamples,
		NumBounces: req.NumBounces,
		X: req.X,
		Y: req.Y,
		CloudMapNamespace: namespaceName,
		CloudMapService: serviceName,
		CloudMapServiceId: serviceId,
		ResultsQueueURL: resultsQueueURL,
	}
}