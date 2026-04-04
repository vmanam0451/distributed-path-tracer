package services

import (
	"context"
	"log"

	"github.com/aws/aws-sdk-go-v2/aws"
	"github.com/aws/aws-sdk-go-v2/service/ecs"
)

func StopAllTasks(ctx context.Context, clusterArn, taskDefinitionArn string, cfg aws.Config) {
	log.Printf("Stopping all ECS tasks for cluster %s with task definition %s", clusterArn, taskDefinitionArn)

	client := ecs.NewFromConfig(cfg)

	paginator := ecs.NewListTasksPaginator(client, &ecs.ListTasksInput{
		Cluster: aws.String(clusterArn),
		Family:  aws.String(taskDefinitionArn),
	})

	for paginator.HasMorePages() {
		page, err := paginator.NextPage(ctx)
		if err != nil {
			log.Printf("Failed to list ECS tasks: %v", err)
			return
		}

		for _, taskArn := range page.TaskArns {
			_, err := client.StopTask(ctx, &ecs.StopTaskInput{
				Cluster: aws.String(clusterArn),
				Task:    aws.String(taskArn),
				Reason:  aws.String("Render cleanup: stopping orphaned worker tasks"),
			})
			if err != nil {
				log.Printf("Failed to stop task %s: %v", taskArn, err)
			} else {
				log.Printf("Stopped ECS task: %s", taskArn)
			}
		}
	}
}
