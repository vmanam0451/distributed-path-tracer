package services

import (
	"context"
	"encoding/json"
	"log"
	"github.com/aws/aws-sdk-go-v2/aws"
	"github.com/aws/aws-sdk-go-v2/service/lambda"
)


type LambdaRequest struct {
	SceneBucket string `json:"scene_bucket"`
	SceneKey string `json:"scene_key"`
	SceneName string `json:"scene_name"`
	NumWorkers int `json:"num_workers"`
	NumSamples int `json:"samples"`
	NumBounces int `json:"bounces"`
	X int `json:"X"`
	Y int `json:"Y"`
	CloudMapNamespace string `json:"cloud_map_namespace"`
	CloudMapService string `json:"cloud_map_service"`
	CloudMapServiceId string `json:"cloud_map_service_id"`
	ResultsQueueURL string `json:"results_queue_url"`
}

func InvokePreprocessorLambda(ctx context.Context, lambdaArn string, lambdaRequest LambdaRequest, cfg aws.Config) error {
	log.Println("Invoking Lambda function with payload:", lambdaRequest)
	
	client := lambda.NewFromConfig(cfg)
	payload, err := payloadToJSON(lambdaRequest)
	if err != nil {
		log.Printf("Failed to convert Lambda payload to JSON: %v", err)
		return err
	}

	input := &lambda.InvokeInput{
		FunctionName:	aws.String(lambdaArn),
		Payload:		[]byte(payload),
	}

	output, err := client.Invoke(ctx, input)
	if err != nil {
		log.Printf("Failed to invoke Lambda function: %v", err)
		return err
	}

	if output.FunctionError != nil {
		log.Printf("Lambda function returned an error: %s", *output.FunctionError)
		return err
	}

	log.Printf("Lambda function invoked successfully: %v", output)
	return nil
}

func payloadToJSON(payload LambdaRequest) (string, error) {
	b, err := json.Marshal(payload)
	if err != nil {
		log.Printf("Failed to marshal Lambda payload: %v", err)
		return "", err
	}
	return string(b), nil
}