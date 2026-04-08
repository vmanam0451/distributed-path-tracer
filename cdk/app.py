#!/usr/bin/env python3


import aws_cdk as cdk
from cdk.path_tracer_stack import PathTracerStack


app = cdk.App()

distributed_path_tracer_stack = PathTracerStack(app, "DistributedPathTracerStack")

app.synth()
