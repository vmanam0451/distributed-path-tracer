#!/usr/bin/env node
import 'source-map-support/register';
import * as cdk from 'aws-cdk-lib';
import { VpcStack } from '../lib/vpc-stack';
import { EcsStack } from '../lib/path-tracer-task-stack';
import { LambdaStack } from '../lib/preprocessor-stack';

const app = new cdk.App();

// Create VPC stack
const vpcStack = new VpcStack(app, 'DistributedPathTracerVpcStack', {
  stackName: 'distributed-path-tracer-vpc-stack',
});

// Create ECS stack that depends on VPC stack
const ecsStack = new EcsStack(app, 'DistributedPathTracerEcsStack', {
  stackName: 'distributed-path-tracer-ecs-stack',
  vpc: vpcStack.vpc,
  ecsSecurityGroup: vpcStack.ecsSecurityGroup,
});
ecsStack.addDependency(vpcStack);

// Create Lambda stack that depends on both VPC and ECS stacks
const lambdaStack = new LambdaStack(app, 'DistributedPathTracerLambdaStack', {
  stackName: 'distributed-path-tracer-lambda-stack',
  vpc: vpcStack.vpc,
  ecsSecurityGroup: vpcStack.ecsSecurityGroup,
  cluster: ecsStack.cluster,
  taskDefinition: ecsStack.taskDefinition
});
lambdaStack.addDependency(ecsStack);