package services

import (
	"context"
	"log"
	"github.com/aws/aws-sdk-go-v2/aws"
	"github.com/aws/aws-sdk-go-v2/service/servicediscovery"
)

func CreateCloudMapService(ctx context.Context, namespaceID, cloudMapService string, cfg aws.Config) (string, error) {

	log.Printf("Creating Cloud Map service: %s in namespace: %s", cloudMapService, namespaceID)

	client := servicediscovery.NewFromConfig(cfg)
	input := &servicediscovery.CreateServiceInput{
		NamespaceId: 	aws.String(namespaceID),
		Name: 			aws.String(cloudMapService),
	}
	
	output, err := client.CreateService(ctx, input)
	if err != nil {
		log.Printf("Failed to create Cloud Map service: %v", err)
		return "", err
	}

	log.Printf("Cloud Map service created: %v", output)
	return *output.Service.Id, nil
}

func DeregisterCloudMapInstances(ctx context.Context, serviceId string, cfg aws.Config) {
	client := servicediscovery.NewFromConfig(cfg)

	paginator := servicediscovery.NewListInstancesPaginator(client, &servicediscovery.ListInstancesInput{
		ServiceId: aws.String(serviceId),
	})

	for paginator.HasMorePages() {
		page, err := paginator.NextPage(ctx)
		if err != nil {
			log.Printf("Failed to list Cloud Map instances for service %s: %v", serviceId, err)
			return
		}
		for _, instance := range page.Instances {
			_, err := client.DeregisterInstance(ctx, &servicediscovery.DeregisterInstanceInput{
				ServiceId:  aws.String(serviceId),
				InstanceId: instance.Id,
			})
			if err != nil {
				log.Printf("Failed to deregister instance %s: %v", *instance.Id, err)
			} else {
				log.Printf("Deregistered Cloud Map instance: %s", *instance.Id)
			}
		}
	}
}

func DeleteCloudMapService(ctx context.Context, serviceId string, cfg aws.Config) {
	log.Printf("Deleting Cloud Map service: %s", serviceId)

	DeregisterCloudMapInstances(ctx, serviceId, cfg)

	client := servicediscovery.NewFromConfig(cfg)
	_, err := client.DeleteService(ctx, &servicediscovery.DeleteServiceInput{
		Id: aws.String(serviceId),
	})
	if err != nil {
		log.Printf("Failed to delete Cloud Map service: %v", err)
	} else {
		log.Printf("Cloud Map service deleted: %s", serviceId)
	}
}