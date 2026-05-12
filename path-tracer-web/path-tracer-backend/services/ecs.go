package services

import (
	"context"
	"encoding/json"
	"fmt"
	"log"
	"os"
	"strings"

	"github.com/aws/aws-sdk-go-v2/aws"
	"github.com/aws/aws-sdk-go-v2/service/ecs"
	ecsTypes "github.com/aws/aws-sdk-go-v2/service/ecs/types"
)

// LaunchWorkerTasks launches a Fargate task for each worker info entry and returns
// the task ARNs of successfully launched tasks.
func LaunchWorkerTasks(ctx context.Context, workerInfos map[string]WorkerInfo, cfg aws.Config) ([]string, error) {
	clusterArn := os.Getenv("ECS_CLUSTER_ARN")
	taskDefinition := os.Getenv("TASK_DEFINITION_ARN")
	subnetIDs := os.Getenv("SUBNET_IDS")
	securityGroupID := os.Getenv("SECURITY_GROUP_ID")

	if clusterArn == "" || taskDefinition == "" || subnetIDs == "" || securityGroupID == "" {
		return nil, fmt.Errorf("missing required ECS environment variables")
	}

	client := ecs.NewFromConfig(cfg)
	var taskArns []string

	for workerID, info := range workerInfos {
		log.Printf("Launching Fargate task for worker %s", workerID)

		workerJSON, err := json.Marshal(info)
		if err != nil {
			log.Printf("Failed to marshal worker info for %s: %v", workerID, err)
			continue
		}

		memory := "16384"
		cpu := "8192"
		if workerID == "master" {
			memory = "8192"
			cpu = "4096"
		}

		resp, err := client.RunTask(ctx, &ecs.RunTaskInput{
			Cluster:        aws.String(clusterArn),
			TaskDefinition: aws.String(taskDefinition),
			Count:          aws.Int32(1),
			LaunchType:     ecsTypes.LaunchTypeFargate,
			NetworkConfiguration: &ecsTypes.NetworkConfiguration{
				AwsvpcConfiguration: &ecsTypes.AwsVpcConfiguration{
					Subnets:        strings.Split(subnetIDs, ","),
					SecurityGroups: []string{securityGroupID},
					AssignPublicIp: ecsTypes.AssignPublicIpDisabled,
				},
			},
			Overrides: &ecsTypes.TaskOverride{
				Cpu:    aws.String(cpu),
				Memory: aws.String(memory),
				ContainerOverrides: []ecsTypes.ContainerOverride{
					{
						Name: aws.String("worker"),
						Environment: []ecsTypes.KeyValuePair{
							{
								Name:  aws.String("WORKER_INFO"),
								Value: aws.String(string(workerJSON)),
							},
						},
					},
				},
			},
		})
		if err != nil {
			log.Printf("Failed to launch task for worker %s: %v", workerID, err)
			continue
		}

		for _, failure := range resp.Failures {
			log.Printf("Task launch failure for worker %s: reason=%s, detail=%s",
				workerID, aws.ToString(failure.Reason), aws.ToString(failure.Detail))
		}

		if len(resp.Tasks) > 0 {
			arn := aws.ToString(resp.Tasks[0].TaskArn)
			taskArns = append(taskArns, arn)
			log.Printf("Launched task %s for worker %s", arn, workerID)
		}
	}

	return taskArns, nil
}

func StopAllTasks(ctx context.Context, clusterArn, taskDefinitionArn string, cfg aws.Config) {
	// Extract family name from ARN like "arn:aws:ecs:...:task-definition/family-name:revision"
	family := taskDefinitionArn
	if idx := strings.LastIndex(family, "/"); idx >= 0 {
		family = family[idx+1:]
	}
	if idx := strings.LastIndex(family, ":"); idx >= 0 {
		family = family[:idx]
	}

	log.Printf("Stopping all ECS tasks for cluster %s with family %s", clusterArn, family)

	client := ecs.NewFromConfig(cfg)

	paginator := ecs.NewListTasksPaginator(client, &ecs.ListTasksInput{
		Cluster: aws.String(clusterArn),
		Family:  aws.String(family),
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

// StopTasks stops only the specified ECS tasks by ARN.
func StopTasks(ctx context.Context, clusterArn string, taskArns []string, cfg aws.Config) {
	if len(taskArns) == 0 {
		return
	}

	log.Printf("Stopping %d ECS tasks for cluster %s", len(taskArns), clusterArn)

	client := ecs.NewFromConfig(cfg)

	for _, taskArn := range taskArns {
		_, err := client.StopTask(ctx, &ecs.StopTaskInput{
			Cluster: aws.String(clusterArn),
			Task:    aws.String(taskArn),
			Reason:  aws.String("Render cleanup: stopping worker tasks"),
		})
		if err != nil {
			log.Printf("Failed to stop task %s: %v", taskArn, err)
		} else {
			log.Printf("Stopped ECS task: %s", taskArn)
		}
	}
}