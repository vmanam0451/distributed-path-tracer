import * as cdk from 'aws-cdk-lib';
import { Construct } from 'constructs';
import * as ec2 from 'aws-cdk-lib/aws-ec2';

export class VpcStack extends cdk.Stack {
  public readonly vpc: ec2.Vpc;
  public readonly ecsSecurityGroup: ec2.SecurityGroup;
  public readonly vpcEndpointSecurityGroup: ec2.SecurityGroup;

  constructor(scope: Construct, id: string, props?: cdk.StackProps) {
    super(scope, id, props);

    // VPC Infrastructure
    this.vpc = new ec2.Vpc(this, "PathTracerVPC", {
      maxAzs: 1,
      subnetConfiguration: [
        { 
          name: "private", 
          subnetType: ec2.SubnetType.PRIVATE_ISOLATED,
          cidrMask: 19
        }
      ],
      cidr: "10.0.0.0/16"
    });

    // Security Groups
    this.vpcEndpointSecurityGroup = new ec2.SecurityGroup(this, 'VPCEndpointSecurityGroup', {
      vpc: this.vpc,
      description: 'Security group for VPC endpoints',
      allowAllOutbound: true
    });

    this.vpcEndpointSecurityGroup.addIngressRule(
      ec2.Peer.ipv4(this.vpc.vpcCidrBlock),
      ec2.Port.tcp(443),
      'HTTPS from within VPC'
    );

    this.ecsSecurityGroup = new ec2.SecurityGroup(this, 'ECSSecurityGroup', {
      vpc: this.vpc,
      description: 'Security group for ECS tasks',
      allowAllOutbound: true
    });

    this.ecsSecurityGroup.addIngressRule(
      ec2.Peer.ipv4(this.vpc.vpcCidrBlock),
      ec2.Port.tcpRange(0, 65535),
      'All TCP traffic from within VPC'
    );

    // VPC Endpoints
    new ec2.GatewayVpcEndpoint(this, 'S3VpcEndpoint', {
      vpc: this.vpc,
      service: ec2.GatewayVpcEndpointAwsService.S3,
    });

    new ec2.InterfaceVpcEndpoint(this, 'SQSVpcEndpoint', {
      vpc: this.vpc,
      service: ec2.InterfaceVpcEndpointAwsService.SQS,
      privateDnsEnabled: true,
      subnets: { subnetType: ec2.SubnetType.PRIVATE_ISOLATED },
      securityGroups: [this.vpcEndpointSecurityGroup]
    });

    new ec2.InterfaceVpcEndpoint(this, 'SNSVpcEndpoint', {
      vpc: this.vpc,
      service: ec2.InterfaceVpcEndpointAwsService.SNS,
      privateDnsEnabled: true,
      subnets: { subnetType: ec2.SubnetType.PRIVATE_ISOLATED },
      securityGroups: [this.vpcEndpointSecurityGroup]
    });

    new ec2.InterfaceVpcEndpoint(this, 'ECRApiVpcEndpoint', {
      vpc: this.vpc,
      service: ec2.InterfaceVpcEndpointAwsService.ECR,
      privateDnsEnabled: true,
      subnets: { subnetType: ec2.SubnetType.PRIVATE_ISOLATED },
      securityGroups: [this.vpcEndpointSecurityGroup]
    });

    new ec2.InterfaceVpcEndpoint(this, 'ECRDkrVpcEndpoint', {
      vpc: this.vpc,
      service: ec2.InterfaceVpcEndpointAwsService.ECR_DOCKER,
      privateDnsEnabled: true,
      subnets: { subnetType: ec2.SubnetType.PRIVATE_ISOLATED },
      securityGroups: [this.vpcEndpointSecurityGroup]
    });

    new ec2.InterfaceVpcEndpoint(this, 'LogsVpcEndpoint', {
      vpc: this.vpc,
      service: ec2.InterfaceVpcEndpointAwsService.CLOUDWATCH_LOGS,
      privateDnsEnabled: true,
      subnets: { subnetType: ec2.SubnetType.PRIVATE_ISOLATED },
      securityGroups: [this.vpcEndpointSecurityGroup]
    });

    // Outputs
    new cdk.CfnOutput(this, 'PrivateSubnetId', {
      value: this.vpc.privateSubnets[0].subnetId,
      description: 'Private Subnet ID',
      exportName: 'PathTracer-PrivateSubnet'
    });

    new cdk.CfnOutput(this, 'ECSSecurityGroupId', {
      value: this.ecsSecurityGroup.securityGroupId,
      description: 'ECS Security Group ID',
      exportName: 'PathTracer-ECSSecurityGroup'
    });
  }
}