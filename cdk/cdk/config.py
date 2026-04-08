class Config:
    # S3 Configuration
    S3_BUCKET_NAME = "distributed-path-tracer"
    
    @classmethod
    def get_s3_bucket_arn(cls):
        return f"arn:aws:s3:::{cls.S3_BUCKET_NAME}"
    
    @classmethod
    def get_s3_object_arn(cls):
        return f"arn:aws:s3:::{cls.S3_BUCKET_NAME}/*"