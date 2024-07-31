import json
import boto3
from typing import List
import yaml
import os

from preprocess.preprocessor import Preprocessor

def create_topic(env, config, topic_name: str):
    session = boto3.session.Session()
    
    sns_config = boto3.session.Config(proxies={"http": config['sns']['url']}) if env == 'local' else None
    sns_client = session.client(
        service_name='sns',
        region_name=config['sns']['region'],
        config=sns_config
    )
    
    try:
        response = sns_client.create_topic(Name=topic_name)
    except Exception as e:
        print("Error creating topic: {}".format(e))
        
    return response

def create_queues(worker_ids: List[int]):

    pass

def lambda_handler(event, context):
    """Sample pure Lambda function

    Parameters
    ----------
    event: dict, required
        API Gateway Lambda Proxy Input Format

        Event doc: https://docs.aws.amazon.com/apigateway/latest/developerguide/set-up-lambda-proxy-integrations.html#api-gateway-simple-proxy-for-lambda-input-format

    context: object, required
        Lambda Context runtime methods and attributes

        Context doc: https://docs.aws.amazon.com/lambda/latest/dg/python-context-object.html

    Returns
    ------
    API Gateway Lambda Proxy Output Format: dict

        Return doc: https://docs.aws.amazon.com/apigateway/latest/developerguide/set-up-lambda-proxy-integrations.html
    """
    
    config_file = "prod.yml" if os.environ.get('prod') else "local.yml"
    env = 'prod' if os.environ.get('prod') else 'local'
    
    with open(config_file, 'r') as stream:
        config = yaml.safe_load(stream)

    preprocessor = Preprocessor(scene_bucket='distributed-path-tracer', scene_root='scenes/sponza-new')    
    split_scene = preprocessor.get_split_scene()
    
    sns_response = create_topic(env, config, '{}-topic'.format('sponza-new'))
    print(sns_response)

    return {
        "statusCode": 200,
        "body": json.dumps(split_scene),
    }
