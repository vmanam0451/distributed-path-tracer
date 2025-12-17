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
   The role needs the following permissions:
   - CloudFormation full access
   - Lambda full access
   - IAM role creation/update
   - S3 access for artifacts bucket
   - ECR access for Docker images
   - API Gateway access
   - SNS and SQS access

   Example policy:
   ```json
   {
     "Version": "2012-10-17",
     "Statement": [
       {
         "Effect": "Allow",
         "Action": [
           "cloudformation:*",
           "lambda:*",
           "iam:*",
           "s3:*",
           "ecr:*",
           "apigateway:*",
           "sns:*",
           "sqs:*",
           "logs:*"
         ],
         "Resource": "*"
       }
     ]
   }
   ```

### 2. AWS Resources

Create the following AWS resources:

1. **S3 Bucket for SAM Artifacts**:
   - Create an S3 bucket to store SAM deployment artifacts
   - Example: `distributed-path-trace-function-artifacts`

2. **ECR Repository for Docker Images**:
   - Create an ECR repository for the Lambda container images
   - Example: `distributed-path-tracer`

### 3. GitHub Repository Configuration

#### Secrets

Add the following secrets to your GitHub repository (Settings → Secrets and variables → Actions → Secrets):

- `AWS_ROLE_ARN`: The ARN of the IAM role created above
  - Example: `arn:aws:iam::123456789012:role/GitHubActionsDeployRole`
- `SAM_ARTIFACTS_BUCKET`: The name of the S3 bucket for SAM artifacts
  - Example: `distributed-path-trace-function-artifacts`
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
