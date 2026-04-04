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