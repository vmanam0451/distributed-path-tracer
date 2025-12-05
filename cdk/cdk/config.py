class Config:
    # S3 Configuration
    S3_BUCKET_NAME = "distributed-path-tracer"
    
    # SNS Configuration
    SNS_TOPIC_PATTERN = "*-distributed-scene-topic"
    
    # SQS Configuration
    SQS_QUEUE_PATTERN = "*-distributed-scene-worker-*"
    
    @classmethod
    def get_s3_bucket_arn(cls):
        return f"arn:aws:s3:::{cls.S3_BUCKET_NAME}"
    
    @classmethod
    def get_s3_object_arn(cls):
        return f"arn:aws:s3:::{cls.S3_BUCKET_NAME}/*"
    
    @classmethod
    def get_sns_topic_arn(cls, region: str, account: str):
        return f"arn:aws:sns:{region}:{account}:{cls.SNS_TOPIC_PATTERN}"
    
    @classmethod
    def get_sqs_queue_arn(cls, region: str, account: str):
        return f"arn:aws:sqs:{region}:{account}:{cls.SQS_QUEUE_PATTERN}"