import * as cdk from 'aws-cdk-lib';
import { Construct } from 'constructs';
import * as lambda from 'aws-cdk-lib/aws-lambda';
import * as ecr from 'aws-cdk-lib/aws-ecr';
import * as iam from 'aws-cdk-lib/aws-iam';
import * as s3 from 'aws-cdk-lib/aws-s3';
import * as ec2 from 'aws-cdk-lib/aws-ec2';
import * as ecs from 'aws-cdk-lib/aws-ecs';
import { DockerImageCode, DockerImageFunction } from 'aws-cdk-lib/aws-lambda';

export interface LambdaStackProps extends cdk.StackProps {
  vpc: ec2.Vpc;
  ecsSecurityGroup: ec2.SecurityGroup;
  cluster: ecs.Cluster;
  taskDefinition: ecs.FargateTaskDefinition;
}

export class LambdaStack extends cdk.Stack {
  public readonly preprocessorFunction: lambda.DockerImageFunction;

  constructor(scope: Construct, id: string, props: LambdaStackProps) {
    super(scope, id, props);

    // Reference ECR repository
    const preprocessorRepo = ecr.Repository.fromRepositoryName(
      this,
      'PreprocessorRepo',
      'distributed-path-tracer-repo'
    );

    // Reference S3 bucket
    const bucket = s3.Bucket.fromBucketName(
      this,
      'DistributedPathTracerBucket',
      'distributed-path-tracer'
    );

    // Preprocessor Lambda
    this.preprocessorFunction = new DockerImageFunction(this, 'PathTracerPreprocessorFunction', {
      functionName: 'distributed-path-tracer-preprocessor',
      code: DockerImageCode.fromEcr(preprocessorRepo),
      timeout: cdk.Duration.minutes(15),
      memorySize: 1024,
      environment: {
        ECS_CLUSTER: props.cluster.clusterName,
        TASK_DEFINITION: props.taskDefinition.taskDefinitionArn,
        SUBNET_ID: props.vpc.privateSubnets[0].subnetId,
        SECURITY_GROUP_ID: props.ecsSecurityGroup.securityGroupId
      }
    });

    // Add permissions to preprocessor lambda
    bucket.grantReadWrite(this.preprocessorFunction);
    
    this.preprocessorFunction.addToRolePolicy(new iam.PolicyStatement({
      actions: ['sns:ListTopics'],
      resources: ['*']
    }));

    this.preprocessorFunction.addToRolePolicy(new iam.PolicyStatement({
      actions: [
        'sns:CreateTopic',
        'sns:Subscribe',
        'sns:ListSubscriptionsByTopic'
      ],
      resources: [`arn:aws:sns:${this.region}:${this.account}:*-distributed-scene-topic`]
    }));

    this.preprocessorFunction.addToRolePolicy(new iam.PolicyStatement({
      actions: [
        'sqs:CreateQueue',
        'sqs:GetQueueAttributes',
        'sqs:SetQueueAttributes',
        'sqs:GetQueueUrl'
      ],
      resources: [`arn:aws:sqs:${this.region}:${this.account}:*-distributed-scene-worker-*`]
    }));

    this.preprocessorFunction.addToRolePolicy(new iam.PolicyStatement({
      actions: ['ecs:RunTask'],
      resources: ['*']
    }));

    this.preprocessorFunction.addToRolePolicy(new iam.PolicyStatement({
      actions: ['iam:PassRole'],
      resources: ['*']
    }));

    // Create function URL for the preprocessor
    const functionUrl = this.preprocessorFunction.addFunctionUrl({
      authType: lambda.FunctionUrlAuthType.NONE,
      cors: {
        allowedOrigins: ['*'],
        allowedMethods: [lambda.HttpMethod.ALL],
        allowedHeaders: ['*']
      }
    });

    // Output function URL
    new cdk.CfnOutput(this, 'PreprocessorFunctionUrl', {
      value: functionUrl.url,
      description: 'URL for the preprocessor function',
      exportName: 'PathTracer-PreprocessorFunctionUrl'
    });
  }
}