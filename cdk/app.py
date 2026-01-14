#!/usr/bin/env python3
import os

import aws_cdk as cdk

from cdk.ecr_stack import EcrStack
from cdk.ecs_stack import EcsStack
from cdk.lambda_stack import LambdaStack

app = cdk.App()

ecr_stack = EcrStack(app, "EcrStack")
ecs_stack = EcsStack(app, "EcsStack", ecr_repository=ecr_stack.repository)
lambda_stack = LambdaStack(app, "LambdaStack", 
                            ecr_repository=ecr_stack.repository,
                            ecs_cluster=ecs_stack.cluster,
                            task_definition=ecs_stack.task_definition,
                            vpc=ecs_stack.vpc,
                            task_security_group=ecs_stack.task_security_group)

ecs_stack.add_dependency(ecr_stack)
lambda_stack.add_dependency(ecr_stack)
lambda_stack.add_dependency(ecs_stack)

app.synth()
