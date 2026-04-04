from aws_cdk import (
    aws_ecs as ecs,
    aws_iam as iam,
    aws_servicediscovery as servicediscovery,
    aws_ecs_patterns as ecsPatterns,
    aws_lambda as _lambda,
)
from constructs import Construct

class WebAppConstruct(Construct):
    
    def __init__(self, scope: Construct, id: str, 
                 lambda_function: _lambda.IFunction,
                 namespace: servicediscovery.IHttpNamespace,
                 **kwargs) -> None:
        super().__init__(scope, id, **kwargs)

        web_service = ecsPatterns.ApplicationLoadBalancedFargateService(
            self, "PathTracerWebService",
            task_image_options=ecsPatterns.ApplicationLoadBalancedTaskImageOptions(
                image=ecs.ContainerImage.from_asset("../path-tracer-web"),   
                container_port=8080,
                environment={
                    "PREPROCESSOR_LAMBDA_ARN": lambda_function.function_arn,
                    "CLOUD_MAP_NAMESPACE_ID": namespace.namespace_id,
                    "CLOUD_MAP_NAMESPACE_NAME": namespace.namespace_name
                }
            ),
            desired_count=1,
            public_load_balancer=True,
        )

        lambda_function.grant_invoke(web_service.task_definition.task_role)

        # SQS + Cloud Map service CRUD permissions
        web_service.task_definition.task_role.add_to_policy(iam.PolicyStatement(
            actions=["sqs:CreateQueue", "sqs:DeleteQueue", "sqs:SendMessage",
                     "sqs:ReceiveMessage", "sqs:DeleteMessage", "sqs:GetQueueUrl"],
            resources=["*"]
        ))
        web_service.task_definition.task_role.add_to_policy(iam.PolicyStatement(
            actions=["servicediscovery:CreateService", "servicediscovery:DeleteService",
                     "servicediscovery:GetService", "servicediscovery:ListServices"],
            resources=["*"]
        ))

