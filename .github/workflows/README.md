# GitHub Actions CI/CD Setup

This repository uses GitHub Actions to automate the deployment of AWS SAM (Serverless Application Model) stacks.

## Prerequisites

Before you can use the automated deployment workflow, you need to set up the following:

### 1. AWS IAM Role for GitHub Actions (OIDC)

Create an IAM role in AWS that GitHub Actions can assume using OpenID Connect (OIDC). This is the recommended approach as it doesn't require storing long-lived AWS credentials.

#### Steps to create the IAM role:

1. **Create an OIDC Identity Provider in AWS**:
   - Go to IAM → Identity providers → Add provider
   - Provider type: OpenID Connect
   - Provider URL: `https://token.actions.githubusercontent.com`
   - Audience: `sts.amazonaws.com`

2. **Create an IAM Role**:
   - Go to IAM → Roles → Create role
   - Select "Web identity"
   - Choose the OIDC provider you just created
   - Audience: `sts.amazonaws.com`
   - Add a trust policy condition to restrict to your repository:

   ```json
   {
     "Version": "2012-10-17",
     "Statement": [
       {
         "Effect": "Allow",
         "Principal": {
           "Federated": "arn:aws:iam::YOUR_ACCOUNT_ID:oidc-provider/token.actions.githubusercontent.com"
         },
         "Action": "sts:AssumeRoleWithWebIdentity",
         "Condition": {
           "StringEquals": {
             "token.actions.githubusercontent.com:aud": "sts.amazonaws.com"
           },
           "StringLike": {
             "token.actions.githubusercontent.com:sub": "repo:YOUR_GITHUB_USERNAME/distributed-path-tracer:*"
           }
         }
       }
     ]
   }
   ```

3. **Attach Policies to the Role**:
   The role needs the following permissions. Here's a more restrictive policy (adjust resource ARNs as needed):
   
   ```json
   {
     "Version": "2012-10-17",
     "Statement": [
       {
         "Sid": "CloudFormationPermissions",
         "Effect": "Allow",
         "Action": [
           "cloudformation:CreateStack",
           "cloudformation:UpdateStack",
           "cloudformation:DeleteStack",
           "cloudformation:DescribeStacks",
           "cloudformation:DescribeStackEvents",
           "cloudformation:DescribeStackResource",
           "cloudformation:DescribeStackResources",
           "cloudformation:GetTemplate",
           "cloudformation:ValidateTemplate",
           "cloudformation:CreateChangeSet",
           "cloudformation:DescribeChangeSet",
           "cloudformation:ExecuteChangeSet",
           "cloudformation:DeleteChangeSet",
           "cloudformation:ListStackResources"
         ],
         "Resource": [
           "arn:aws:cloudformation:*:*:stack/distributed-path-tracer-stack/*",
           "arn:aws:cloudformation:*:*:stack/distributed-path-tracer-stack-*/*"
         ]
       },
       {
         "Sid": "LambdaPermissions",
         "Effect": "Allow",
         "Action": [
           "lambda:CreateFunction",
           "lambda:DeleteFunction",
           "lambda:UpdateFunctionCode",
           "lambda:UpdateFunctionConfiguration",
           "lambda:GetFunction",
           "lambda:GetFunctionConfiguration",
           "lambda:AddPermission",
           "lambda:RemovePermission",
           "lambda:InvokeFunction",
           "lambda:CreateFunctionUrlConfig",
           "lambda:UpdateFunctionUrlConfig",
           "lambda:DeleteFunctionUrlConfig",
           "lambda:GetFunctionUrlConfig",
           "lambda:TagResource",
           "lambda:UntagResource"
         ],
         "Resource": "arn:aws:lambda:*:*:function:distributed-path-tracer-*"
       },
       {
         "Sid": "IAMPermissions",
         "Effect": "Allow",
         "Action": [
           "iam:CreateRole",
           "iam:DeleteRole",
           "iam:GetRole",
           "iam:PassRole",
           "iam:AttachRolePolicy",
           "iam:DetachRolePolicy",
           "iam:PutRolePolicy",
           "iam:DeleteRolePolicy",
           "iam:GetRolePolicy",
           "iam:TagRole",
           "iam:UntagRole"
         ],
         "Resource": "arn:aws:iam::*:role/distributed-path-tracer-*"
       },
       {
         "Sid": "S3Permissions",
         "Effect": "Allow",
         "Action": [
           "s3:GetObject",
           "s3:PutObject",
           "s3:DeleteObject",
           "s3:ListBucket",
           "s3:GetBucketLocation"
         ],
         "Resource": [
           "arn:aws:s3:::distributed-path-tracer-*",
           "arn:aws:s3:::distributed-path-tracer-*/*"
         ]
       },
       {
         "Sid": "ECRPermissions",
         "Effect": "Allow",
         "Action": [
           "ecr:GetAuthorizationToken",
           "ecr:BatchCheckLayerAvailability",
           "ecr:GetDownloadUrlForLayer",
           "ecr:BatchGetImage",
           "ecr:PutImage",
           "ecr:InitiateLayerUpload",
           "ecr:UploadLayerPart",
           "ecr:CompleteLayerUpload",
           "ecr:DescribeRepositories",
           "ecr:CreateRepository",
           "ecr:SetRepositoryPolicy"
         ],
         "Resource": "*"
       },
       {
         "Sid": "APIGatewayPermissions",
         "Effect": "Allow",
         "Action": [
           "apigateway:POST",
           "apigateway:GET",
           "apigateway:PATCH",
           "apigateway:DELETE",
           "apigateway:PUT"
         ],
         "Resource": "arn:aws:apigateway:*::/*"
       },
       {
         "Sid": "SNSSQSPermissions",
         "Effect": "Allow",
         "Action": [
           "sns:CreateTopic",
           "sns:DeleteTopic",
           "sns:GetTopicAttributes",
           "sns:SetTopicAttributes",
           "sns:Subscribe",
           "sns:Unsubscribe",
           "sns:ListTopics",
           "sns:TagResource",
           "sns:UntagResource",
           "sqs:CreateQueue",
           "sqs:DeleteQueue",
           "sqs:GetQueueAttributes",
           "sqs:SetQueueAttributes",
           "sqs:TagQueue",
           "sqs:UntagQueue"
         ],
         "Resource": "*"
       },
       {
         "Sid": "CloudWatchLogsPermissions",
         "Effect": "Allow",
         "Action": [
           "logs:CreateLogGroup",
           "logs:CreateLogStream",
           "logs:PutLogEvents",
           "logs:DescribeLogGroups",
           "logs:DeleteLogGroup",
           "logs:TagResource",
           "logs:UntagResource"
         ],
         "Resource": "arn:aws:logs:*:*:log-group:/aws/lambda/distributed-path-tracer-*"
       }
     ]
   }
   ```
   
   Note: Some resources like ECR authorization, SNS/SQS (with dynamic naming), require `*` in the resource field. Adjust ARNs based on your account and naming conventions.

### 2. AWS Resources

Create the following AWS resources:

1. **S3 Bucket for SAM Artifacts**:
   - Create an S3 bucket to store SAM deployment artifacts
   - Example: `distributed-path-tracer-artifacts`

2. **ECR Repository for Docker Images**:
   - Create an ECR repository for the Lambda container images
   - Example: `distributed-path-tracer`

### 3. GitHub Repository Configuration

#### Secrets

Add the following secrets to your GitHub repository (Settings → Secrets and variables → Actions → Secrets):

- `AWS_ROLE_ARN`: The ARN of the IAM role created above
  - Example: `arn:aws:iam::123456789012:role/GitHubActionsDeployRole`
- `SAM_ARTIFACTS_BUCKET`: The name of the S3 bucket for SAM artifacts
  - Example: `distributed-path-tracer-artifacts`
- `ECR_REPOSITORY`: The URI of the ECR repository
  - Example: `123456789012.dkr.ecr.us-east-1.amazonaws.com/distributed-path-tracer`

#### Variables (Optional)

Add the following variables (Settings → Secrets and variables → Actions → Variables):

- `AWS_REGION`: AWS region for deployment (default: `us-east-1`)

#### Environments (Recommended)

For better control, create GitHub environments:

1. Go to Settings → Environments
2. Create an environment named `prod`
3. (Optional) Add protection rules like required reviewers
4. Add environment-specific secrets/variables if needed

## Usage

### Manual Deployment

Deployments are triggered manually for better control and to prevent accidental production deployments:

1. Go to Actions → Deploy SAM Application
2. Click "Run workflow"
3. Select the branch and environment
4. Click "Run workflow"

## Workflow Details

The deployment workflow performs the following steps:

1. **Checkout code**: Retrieves the repository code
2. **Configure AWS credentials**: Assumes the IAM role using OIDC
3. **Setup SAM CLI**: Installs the AWS SAM CLI
4. **SAM Build**: Builds the SAM application (including Docker images)
5. **SAM Deploy**: Deploys the stack to AWS
6. **Output Stack Info**: Displays the stack outputs (API endpoints, Lambda ARNs, etc.)

## Troubleshooting

### Build Failures

- Check that all required build tools are available
- Verify Docker images build correctly locally first
- Review CloudWatch logs for Lambda build errors

### Deployment Failures

- Verify IAM role has sufficient permissions
- Check that S3 bucket and ECR repository exist
- Ensure region settings are correct
- Review CloudFormation events in AWS Console

### Authentication Issues

- Verify OIDC provider is correctly configured
- Check IAM role trust policy matches your repository
- Ensure `AWS_ROLE_ARN` secret is correct

## Local Development

To deploy locally:

```bash
# Build the SAM application
sam build --cached

# Deploy with your AWS profile
sam deploy --profile your-profile-name
```

## Additional Resources

- [AWS SAM Documentation](https://docs.aws.amazon.com/serverless-application-model/)
- [GitHub Actions OIDC with AWS](https://docs.github.com/en/actions/deployment/security-hardening-your-deployments/configuring-openid-connect-in-amazon-web-services)
- [SAM CLI Reference](https://docs.aws.amazon.com/serverless-application-model/latest/developerguide/serverless-sam-cli-command-reference.html)
