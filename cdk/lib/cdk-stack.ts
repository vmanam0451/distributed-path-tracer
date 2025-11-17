import * as cdk from 'aws-cdk-lib';
import { Construct } from 'constructs';
import * as ecs from 'aws-cdk-lib/aws-ecs';
import * as lambda from 'aws-cdk-lib/aws-lambda';
export class CdkStack extends cdk.Stack {
  constructor(scope: Construct, id: string, props?: cdk.StackProps) {
    super(scope, id, props);

    const vpc  = new cdk.aws_ec2.Vpc(this, "PathTracerVPC", {
      maxAzs: 1,
      subnetConfiguration: [
        { name: "private", subnetType: cdk.aws_ec2.SubnetType.PRIVATE_ISOLATED }
      ]
    });

    const taskDefinition = new ecs.FargateTaskDefinition(this, "PathTracerTaskDef", {
      memoryLimitMiB: 2048,
      cpu: 1024,
    });

    taskDefinition.addContainer("PathTracerContainer", {
      image: ecs.ContainerImage.fromEcrRepository(
        cdk.aws_ecr.Repository.fromRepositoryName(this, "distributed-path-tracer-repo", "path-tracer-core")
      ),
      logging: ecs.LogDrivers.awsLogs({ streamPrefix: "PathTracer" }),
    });
  }
}
