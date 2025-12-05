from aws_cdk import (
    Stack,
    aws_ecr as ecr,
    RemovalPolicy
)
from constructs import Construct

class EcrStack(Stack):
    
    def __init__(self, scope: Construct, construct_id: str, **kwargs) -> None:
        super().__init__(scope, construct_id, **kwargs)

        repository = ecr.Repository(
            self, "DistributedPathTracerRepo",
            repository_name="distributed-path-tracer-repo",
            removal_policy=RemovalPolicy.DESTROY
        )

        self.repository = repository