#!/usr/bin/env python3
import os

import aws_cdk as cdk

from cdk.ecs_stack import EcsStack
from cdk.lambda_stack import LambdaStack

app = cdk.App()

ecs_stack = EcsStack(app, "EcsStack")
lambda_stack = LambdaStack(app, "LambdaStack", 
                            ecs_cluster=ecs_stack.cluster,
                            task_definition=ecs_stack.task_definition,
                            vpc=ecs_stack.vpc,
                            task_security_group=ecs_stack.task_security_group,
                            namespace=ecs_stack.namespace)

lambda_stack.add_dependency(ecs_stack)

app.synth()
