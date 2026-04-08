

from aws_cdk import Stack
from constructs import Construct

from cdk.path_tracer_worker_construct import PathTracerWorkerConstruct
from cdk.preprocessor_construct import PreprocessorConstruct
from cdk.web_app_construct import WebAppConstruct


class PathTracerStack(Stack):
    def __init__(self, scope: Construct, id: str, **kwargs) -> None:
        super().__init__(scope, id, **kwargs)


        worker = PathTracerWorkerConstruct(self, "PathTracerWorkerConstruct")
        preprocessor = PreprocessorConstruct(self, "PreprocessorConstruct",
                                            ecs_cluster=worker.cluster,
                                            task_definition=worker.task_definition,
                                            vpc=worker.vpc,
                                            task_security_group=worker.task_security_group)
        
        web_app = WebAppConstruct(self, "WebAppConstruct",
                              lambda_function=preprocessor.lambda_function,
                              namespace=worker.namespace,
                              ecs_cluster=worker.cluster,
                              task_definition=worker.task_definition)
        