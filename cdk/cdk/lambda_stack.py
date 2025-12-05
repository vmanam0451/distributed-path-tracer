from aws_cdk import (
    Stack,
    aws_lambda as _lambda,
    aws_iam as iam,
    aws_ecr as ecr,
    aws_apigateway as apigw,
    aws_ecs as ecs,
    aws_ec2 as ec2,
    Duration
)

from config import Config
from constructs import Construct

class LambdaStack(Stack):

    def __init__(self, scope: Construct, construct_id: str, 
                 ecr_repository: ecr.IRepository, 
                 ecs_cluster: ecs.ICluster,
                 task_definition: ecs.TaskDefinition,
                 vpc: ec2.IVpc,
                 task_security_group: ec2.ISecurityGroup,
                 **kwargs) -> None:
        super().__init__(scope, construct_id, **kwargs)

        subnet_ids = [subnet.subnet_id for subnet in vpc.isolated_subnets]

        lambda_function = _lambda.Function(
            self, "DistributedPathTracerFunction",
            function_name="DistributedPathTracerFunction",
            architecture=_lambda.Architecture.X86_64,
            code=_lambda.EcrImageCode.from_ecr(ecr_repository, tag_or_digest="preprocessor-latest"),
            handler=_lambda.Handler.FROM_IMAGE,
            runtime=_lambda.Runtime.FROM_IMAGE,
            memory_size=1024,
            timeout=Duration.minutes(15),
            environment={
                "ECS_CLUSTER_ARN": ecs_cluster.cluster_arn,
                "TASK_DEFINITION_ARN": task_definition.task_definition_arn,
                "SUBNET_IDS": ",".join(subnet_ids),
                "SECURITY_GROUP_ID": task_security_group.security_group_id
            }
        )

        lambda_function.add_to_role_policy(iam.PolicyStatement(
            actions=["s3:PutObject", "s3:GetObject"],
            resources=[Config.get_s3_object_arn()]
        ))

        lambda_function.add_to_role_policy(iam.PolicyStatement(
            actions=["sns:CreateTopic", "sns:Subscribe", "sns:ListSubscriptionsByTopic"],
            resources=[Config.get_sns_topic_arn(self.region, self.account)]
        ))

        lambda_function.add_to_role_policy(iam.PolicyStatement(
            actions=["sns:ListTopics"],
            resources=["*"]
        ))

        lambda_function.add_to_role_policy(iam.PolicyStatement(
            actions=["sqs:CreateQueue", "sqs:GetQueueAttributes", "sqs:SetQueueAttributes", "sqs:GetQueueUrl"],
            resources=[Config.get_sqs_queue_arn(self.region, self.account)]
        ))

        lambda_function.add_to_role_policy(iam.PolicyStatement(
            actions=["ecs:RunTask"],
            resources=[task_definition.task_definition_arn],
        ))

        lambda_function.add_to_role_policy(iam.PolicyStatement(
            actions=["iam:PassRole"],
            resources=["*"], 
            conditions={
                "StringLike": {
                    "iam:PassedToService": "ecs-tasks.amazonaws.com"
                }
            }
        ))

        api = apigw.LambdaRestApi(
            self, "DistributedPathTracerPreprocessAPI",
            handler=lambda_function,
            proxy=False
        )

        preprocess = api.root.add_resource("preprocess")
        preprocess.add_method("POST") 

        self.lambda_function = lambda_function