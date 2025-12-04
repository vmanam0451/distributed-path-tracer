from aws_cdk import (
    Stack,
    aws_lambda as _lambda,
    aws_iam as iam,
    aws_ecr as ecr,
    aws_apigateway as apigw,
    cdk
)

from constructs import Construct

class LambdaStack(Stack):

    def __init__(self, scope: Construct, construct_id: str, ecr_repository: ecr.IRepository, **kwargs) -> None:
        super().__init__(scope, construct_id, **kwargs)

        lambda_function = _lambda.Function(
            self, "DistributedPathTracerFunction",
            function_name="DistributedPathTracerFunction",
            architecture=_lambda.Architecture.X86_64,
            code=_lambda.EcrImageCode.from_ecr(ecr_repository, tag_or_digest="preprocessor-latest"),
            handler=_lambda.Handler.FROM_IMAGE,
            runtime=_lambda.Runtime.FROM_IMAGE,
            memory_size=1024,
            timeout=cdk.Duration.minutes(15),
        )

        lambda_function.add_to_role_policy(iam.PolicyStatement(
            actions=["sns:CreateTopic", "sns:Subscribe", "sns:ListSubscriptionsByTopic"],
            resources=[f"arn:aws:sns:{self.region}:{self.account}:*-distributed-scene-topic"]
        ))

        lambda_function.add_to_role_policy(iam.PolicyStatement(
            actions=["sns:ListTopics"],
            resources=["*"]
        ))

        lambda_function.add_to_role_policy(iam.PolicyStatement(
            actions=["sqs:CreateQueue", "sqs:GetQueueAttributes", "sqs:SetQueueAttributes", "sqs:GetQueueUrl"],
            resources=[f"arn:aws:sqs:{self.region}:{self.account}:*-distributed-scene-worker-*"]
        ))

        lambda_function.add_to_role_policy(iam.PolicyStatement(
            actions=["ecs:RunTask"],
            resources=["*"]
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
        preprocess.add_method("GET") 

        self.lambda_function = lambda_function