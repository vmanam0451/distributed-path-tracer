from aws_cdk import (
    aws_ecs as ecs,
    aws_ec2 as ec2,
    aws_iam as iam,
    aws_servicediscovery as servicediscovery,
    aws_ecs_patterns as ecsPatterns,
    Duration,
)
from cdk.config import Config
from constructs import Construct

# Port the web backend binds its TCP pixel listener on. The master dials
# this port to push PIXEL_BATCH messages directly, replacing the old SQS
# results queue.
WEB_PIXEL_TCP_PORT = 9100

class WebAppConstruct(Construct):

    def __init__(self, scope: Construct, id: str,
                 namespace: servicediscovery.IHttpNamespace,
                 ecs_cluster: ecs.ICluster,
                 task_definition: ecs.TaskDefinition,
                 vpc: ec2.IVpc,
                 task_security_group: ec2.ISecurityGroup,
                 **kwargs) -> None:
        super().__init__(scope, id, **kwargs)

        subnet_ids = [subnet.subnet_id for subnet in vpc.isolated_subnets]

        web_service = ecsPatterns.ApplicationLoadBalancedFargateService(
            self, "PathTracerWebService",
            task_image_options=ecsPatterns.ApplicationLoadBalancedTaskImageOptions(
                image=ecs.ContainerImage.from_asset("../path-tracer-web"),
                container_port=8080,
                environment={
                    "CLOUD_MAP_NAMESPACE_ID": namespace.namespace_id,
                    "CLOUD_MAP_NAMESPACE_NAME": namespace.namespace_name,
                    "ECS_CLUSTER_ARN": ecs_cluster.cluster_arn,
                    "TASK_DEFINITION_ARN": task_definition.task_definition_arn,
                    "SUBNET_IDS": ",".join(subnet_ids),
                    "SECURITY_GROUP_ID": task_security_group.security_group_id,
                    # Pixel-listener wiring. The host has to be something the
                    # workers can reach from inside the VPC; using the task's
                    # ECS metadata-derived private IP is set at runtime by the
                    # backend if WEB_ADVERTISED_HOST is unset, but for
                    # ApplicationLoadBalancedFargateService the easiest path
                    # is to register the task in service discovery and resolve
                    # by hostname. The simplest working default is to allow
                    # operators to override; otherwise the backend will fall
                    # back to 127.0.0.1, which only works for single-host dev.
                    "WEB_TCP_PORT": str(WEB_PIXEL_TCP_PORT),
                }
            ),
            desired_count=1,
            public_load_balancer=True,
            health_check_grace_period=Duration.seconds(60),
            idle_timeout=Duration.seconds(1800), # 30 min to accommodate long renders without client disconnects
        )

        # Allow worker tasks (which run in `task_security_group`) to dial the
        # backend's pixel listener.
        web_service.service.connections.allow_from(
            other=task_security_group,
            port_range=ec2.Port.tcp(WEB_PIXEL_TCP_PORT),
            description="Master dials backend pixel listener",
        )

        web_service.target_group.configure_health_check(
            path="/api/ping",
            interval=Duration.seconds(120),
            timeout=Duration.seconds(10),
            healthy_threshold_count=2,
            unhealthy_threshold_count=3,
        )

        web_service.task_definition.task_role.add_to_policy(iam.PolicyStatement(
            actions=["s3:GetObject", "s3:HeadObject"],
            resources=[Config.get_s3_object_arn()]
        ))

        web_service.task_definition.task_role.add_to_policy(iam.PolicyStatement(
            actions=["s3:ListBucket"],
            resources=[Config.get_s3_bucket_arn()]
        ))

        web_service.task_definition.task_role.add_to_policy(iam.PolicyStatement(
            actions=["servicediscovery:CreateService", "servicediscovery:DeleteService",
                     "servicediscovery:GetService", "servicediscovery:ListServices",
                     "servicediscovery:ListInstances", "servicediscovery:DeregisterInstance"],
            resources=["*"]
        ))

        web_service.task_definition.task_role.add_to_policy(iam.PolicyStatement(
            actions=["ecs:RunTask", "ecs:ListTasks", "ecs:StopTask"],
            resources=["*"]
        ))

        # PassRole so RunTask can assign execution/task roles to workers
        web_service.task_definition.task_role.add_to_policy(iam.PolicyStatement(
            actions=["iam:PassRole"],
            resources=[
                task_definition.execution_role.role_arn,
                task_definition.task_role.role_arn,
            ],
            conditions={
                "StringLike": {
                    "iam:PassedToService": "ecs-tasks.amazonaws.com"
                }
            }
        ))