package services

import (
	"context"
	"log"

	"github.com/aws/aws-sdk-go-v2/aws"
	"github.com/aws/aws-sdk-go-v2/service/sqs"
	"github.com/aws/aws-sdk-go-v2/service/sqs/types"
)

func CreateSQSQueue(ctx context.Context, queueName string, cfg aws.Config) (string, error) {
	log.Printf("Creating SQS queue: %s", queueName)

	client := sqs.NewFromConfig(cfg)

	sqsInput := &sqs.CreateQueueInput{
		QueueName: aws.String(queueName),
		Attributes: map[string]string{
			"ReceiveMessageWaitTimeSeconds": "20", // Enable long polling
		},
	}

	sqsOutput, err := client.CreateQueue(ctx, sqsInput)
	if err != nil {
		log.Printf("Failed to create SQS queue: %v", err)
		return "", err
	}

	log.Printf("SQS queue created: %v", sqsOutput.QueueUrl)
	return *sqsOutput.QueueUrl, nil
}

func DeleteSQSQueue(ctx context.Context, queueURL string, cfg aws.Config) error {
	log.Printf("Deleting SQS queue: %s", queueURL)
	
	client := sqs.NewFromConfig(cfg)
	_, err := client.DeleteQueue(ctx, &sqs.DeleteQueueInput{
		QueueUrl: aws.String(queueURL),
	})
	if err != nil {
		log.Printf("Failed to delete SQS queue: %v", err)
		return err
	}

	log.Printf("SQS queue deleted: %s", queueURL)
	return nil
}

func PollSQSQueue(ctx context.Context, queueURL string, cfg aws.Config, onMessage func(message string)) error {
	log.Printf("Polling SQS queue: %s", queueURL)

	client := sqs.NewFromConfig(cfg)
	for {
		output, err := client.ReceiveMessage(ctx, &sqs.ReceiveMessageInput{
			QueueUrl:              aws.String(queueURL),
			MaxNumberOfMessages:   10,
			WaitTimeSeconds:       20, // Enable long polling
			MessageAttributeNames: []string{"ALL"},
		})
		if err != nil {
			log.Printf("Failed to receive messages from SQS queue: %v", err)
			return err
		}

		for _, message := range output.Messages {
			log.Printf("Received message from SQS queue: %s", *message.Body)
			if _, ok := message.MessageAttributes["Terminate"]; ok {
				log.Printf("Termination message received, stopping SQS polling")
				deleteSQSMessage(ctx, message, client, queueURL)
				return nil
			}

			onMessage(*message.Body)
			deleteSQSMessage(ctx, message, client, queueURL)
		}
	}
}

func deleteSQSMessage(ctx context.Context, msg types.Message, sqsClient *sqs.Client, queueURL string) {
	log.Printf("Deleting message from SQS queue: %s", *msg.MessageId)
	_, err := sqsClient.DeleteMessage(ctx, &sqs.DeleteMessageInput{
		QueueUrl:      aws.String(queueURL),
		ReceiptHandle: msg.ReceiptHandle,
	})
	if err != nil {
		log.Printf("Failed to delete message from SQS queue: %v", err)
	}
}
