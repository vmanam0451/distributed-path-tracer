from aws_cdk import (
    Stack,
    aws_ecs as ecs,
    aws_ec2 as ec2,
    aws_iam as iam,
    Duration
)

from cdk.config import Config
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

        task_security_group = ec2.SecurityGroup(
            self, "TaskSecurityGroup",
            vpc=vpc,
            description="Security group for ECS tasks"
        )
        
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
            service=ec2.GatewayVpcEndpointAwsService.S3
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
            service=ec2.InterfaceVpcEndpointAwsService.ECR,
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

        task_execution_role = iam.Role(
            self, "TaskExecutionRole",
            assumed_by=iam.ServicePrincipal("ecs-tasks.amazonaws.com"),
            managed_policies=[
                iam.ManagedPolicy.from_aws_managed_policy_name("service-role/AmazonECSTaskExecutionRolePolicy")
            ]
        )

        task_role = iam.Role(
            self, "TaskRole",
            assumed_by=iam.ServicePrincipal("ecs-tasks.amazonaws.com")
        )

        task_role.add_to_policy(iam.PolicyStatement(
            actions=["s3:GetObject", "s3:PutObject"],
            resources=[Config.get_s3_object_arn()]
        ))

        task_role.add_to_policy(iam.PolicyStatement(
            actions=["sns:Publish"],
            resources=[Config.get_sns_topic_arn(self.region, self.account)]
        ))

        task_role.add_to_policy(iam.PolicyStatement(
            actions=["sqs:ReceiveMessage", "sqs:DeleteMessage", "sqs:Get*"],
            resources=[Config.get_sqs_queue_arn(self.region, self.account)]
        ))

        task_definition = ecs.FargateTaskDefinition(
            self, "DistributedPathTracerWorker",
            family="distributed-path-tracer-worker",
            memory_limit_mib=2048,
            cpu=1024,
            execution_role=task_execution_role,
            task_role=task_role
        )

        container = task_definition.add_container(
            "worker",
            image=ecs.ContainerImage.from_asset("../path-tracer-core"),
            logging=ecs.LogDrivers.aws_logs(stream_prefix="DistributedPathTracerWorker"),
            stop_timeout=Duration.seconds(10),
        )

        self.cluster = cluster
        self.task_definition = task_definition
        self.task_security_group = task_security_group
        self.vpc = vpc
