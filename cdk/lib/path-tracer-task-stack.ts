import * as cdk from 'aws-cdk-lib';
import { Construct } from 'constructs';
import * as ec2 from 'aws-cdk-lib/aws-ec2';
import * as ecs from 'aws-cdk-lib/aws-ecs';
import * as ecr from 'aws-cdk-lib/aws-ecr';
import * as iam from 'aws-cdk-lib/aws-iam';
import * as logs from 'aws-cdk-lib/aws-logs';

export interface EcsStackProps extends cdk.StackProps {
  vpc: ec2.Vpc;
  ecsSecurityGroup: ec2.SecurityGroup;
}

export class EcsStack extends cdk.Stack {
  public readonly cluster: ecs.Cluster;
  public readonly taskDefinition: ecs.FargateTaskDefinition;
  public readonly taskRole: iam.Role;
  public readonly executionRole: iam.Role;

  constructor(scope: Construct, id: string, props: EcsStackProps) {
    super(scope, id, props);

    // ECS Cluster
    this.cluster = new ecs.Cluster(this, 'PathTracerCluster', {
      vpc: props.vpc,
      clusterName: 'PathTracerCluster',
      containerInsights: true,
    });

    // IAM Roles for ECS
    this.executionRole = new iam.Role(this, 'ECSTaskExecutionRole', {
      assumedBy: new iam.ServicePrincipal('ecs-tasks.amazonaws.com'),
      managedPolicies: [
        iam.ManagedPolicy.fromAwsManagedPolicyName('service-role/AmazonECSTaskExecutionRolePolicy')
      ]
    });

    this.taskRole = new iam.Role(this, 'ECSTaskRole', {
      assumedBy: new iam.ServicePrincipal('ecs-tasks.amazonaws.com'),
      managedPolicies: [
        iam.ManagedPolicy.fromAwsManagedPolicyName('AmazonS3FullAccess'),
        iam.ManagedPolicy.fromAwsManagedPolicyName('AmazonSNSFullAccess'),
        iam.ManagedPolicy.fromAwsManagedPolicyName('AmazonSQSFullAccess')
      ]
    });

    // Log Group
    const logGroup = new logs.LogGroup(this, 'PathTracerLogGroup', {
      logGroupName: '/ecs/path-tracer',
      retention: logs.RetentionDays.ONE_DAY,
      removalPolicy: cdk.RemovalPolicy.DESTROY
    });

    // Task Definition
    this.taskDefinition = new ecs.FargateTaskDefinition(this, 'PathTracerTaskDef', {
      family: 'path-tracer-worker',
      memoryLimitMiB: 2048,
      cpu: 1024,
      taskRole: this.taskRole,
      executionRole: this.executionRole,
    });

    const containerRepo = ecr.Repository.fromRepositoryName(
      this, 
      'PathTracerRepo',
      'distributed-path-tracer-repo'
    );

    this.taskDefinition.addContainer('worker', {
      image: ecs.ContainerImage.fromEcrRepository(containerRepo, 'latest'),
      logging: ecs.LogDrivers.awsLogs({
        logGroup,
        streamPrefix: 'worker'
      }),
      essential: true
    });

    // Outputs
    new cdk.CfnOutput(this, 'ClusterName', {
      value: this.cluster.clusterName,
      description: 'ECS Cluster Name',
      exportName: 'PathTracer-ClusterName'
    });

    new cdk.CfnOutput(this, 'ClusterArn', {
      value: this.cluster.clusterArn,
      description: 'ECS Cluster ARN',
      exportName: 'PathTracer-ClusterArn'
    });

    new cdk.CfnOutput(this, 'TaskDefinitionArn', {
      value: this.taskDefinition.taskDefinitionArn,
      description: 'Task Definition ARN',
      exportName: 'PathTracer-TaskDefinitionArn'
    });
  }
}