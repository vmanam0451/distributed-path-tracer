from aws_cdk import (
    Stack,
    aws_ecs as ecs,
    aws_ec2 as ec2,
)

from constructs import Construct

class EcsStack(Stack):
    
    def __init__(self, scope: Construct, construct_id: str, **kwargs) -> None:
        super().__init__(scope, construct_id, **kwargs)

        vpc = ec2.Vpc(
            self, "DistributedPathTracerVPC",
            subnet_configuration = [
                ec2.SubnetConfiguration(
                    name="Private",
                    subnet_type=ec2.SubnetType.PRIVATE_ISOLATED,
                    cidr_mask=24
                )
            ],
            max_azs=1)
        
        endpoint_security_group = ec2.SecurityGroup(
            self, "EndpointSecurityGroup",
            vpc=vpc,
            description="Security group for VPC endpoints"
        )

        endpoint_security_group.add_ingress_rule(
            peer=task_security_group,
            connection=ec2.Port.tcp(443),
            description="Allow HTTPS from ECS tasks"
        )

        
        vpc.add_gateway_endpoint(
            "S3Endpoint",
            service=ec2.GatewayVpcEndpointAwsService.S3,
            security_groups=[endpoint_security_group]
        )

        vpc.add_interface_endpoint(
            "SQSEndpoint",
            service=ec2.InterfaceVpcEndpointAwsService.SQS,
            security_groups=[endpoint_security_group]
        )

        vpc.add_interface_endpoint(
            "SNSEndpoint",
            service=ec2.InterfaceVpcEndpointAwsService.SNS,
            security_groups=[endpoint_security_group]
        )

        vpc.add_interface_endpoint(
            "ECRApiEndpoint",
            service=ec2.InterfaceVpcEndpointAwsService.ECR_API,
            security_groups=[endpoint_security_group]
        )

        vpc.add_interface_endpoint(
            "ECRDockerEndpoint",
            service=ec2.InterfaceVpcEndpointAwsService.ECR_DOCKER,
            security_groups=[endpoint_security_group]
        )

        vpc.add_interface_endpoint(
            "CloudWatchLogsEndpoint",
            service=ec2.InterfaceVpcEndpointAwsService.CLOUDWATCH_LOGS,
            security_groups=[endpoint_security_group]
        )
        
        cluster = ecs.Cluster(
            self, "DistributedPathTracerCluster",
            cluster_name="DistributedPathTracerCluster",
            vpc=vpc
        )
