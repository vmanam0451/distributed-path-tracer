from aws_cdk import (
    aws_lambda as _lambda,
    aws_iam as iam,
    aws_ecs as ecs,
    aws_ec2 as ec2,
    aws_logs as logs,
    Duration,
    RemovalPolicy
)

from cdk.config import Config
from constructs import Construct

class PreprocessorConstruct(Construct):

    def __init__(self, scope: Construct, id: str, 
                 ecs_cluster: ecs.ICluster,
                 task_definition: ecs.TaskDefinition,
                 vpc: ec2.IVpc,
                 task_security_group: ec2.ISecurityGroup,
                 **kwargs) -> None:
        super().__init__(scope, id, **kwargs)

        subnet_ids = [subnet.subnet_id for subnet in vpc.isolated_subnets]

        log_group = logs.LogGroup(
            self, "LambdaLogGroup",
            log_group_name="/aws/lambda/DistributedPathTracerFunction",
            removal_policy=RemovalPolicy.DESTROY,
            retention=logs.RetentionDays.ONE_WEEK
        )
        
        # Build environment variables
        env_vars = {
            "ECS_CLUSTER_ARN": ecs_cluster.cluster_arn,
            "TASK_DEFINITION_ARN": task_definition.task_definition_arn,
            "SUBNET_IDS": ",".join(subnet_ids),
            "SECURITY_GROUP_ID": task_security_group.security_group_id
        }
        
        lambda_function = _lambda.DockerImageFunction(
            self, "DistributedPathTracerFunction",
            architecture=_lambda.Architecture.X86_64,
            code=_lambda.DockerImageCode.from_image_asset("../path-tracer-preprocessor/preprocessor-function"),
            memory_size=1024,
            timeout=Duration.minutes(15),
            log_group=log_group,
            environment=env_vars
        )

        lambda_function.add_to_role_policy(iam.PolicyStatement(
            actions=["s3:PutObject", "s3:GetObject"],
            resources=[Config.get_s3_object_arn()]
        ))

        lambda_function.add_to_role_policy(iam.PolicyStatement(
            actions=["ecs:RunTask"],
            resources=[task_definition.task_definition_arn],
        ))

        lambda_function.add_to_role_policy(iam.PolicyStatement(
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
        
        self.lambda_function = lambda_function