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
    function_input = event['body']
    
    scene_bucket = function_input['scene_bucket']
    scene_key = function_input['scene_key']
    scene_name = function_input['scene_name']
    
    config_file = "prod.yml" if os.environ.get('prod') else "local.yml"
    env = 'prod' if os.environ.get('prod') else 'local'
    
    with open(config_file, 'r') as stream:
        config = yaml.safe_load(stream)

    preprocessor = Preprocessor(scene_bucket=scene_bucket, scene_root=scene_key)    
    split_scene = preprocessor.get_split_scene()
    
    sns_response = create_topic(env, config, '{}-topic'.format(scene_name))
    print(sns_response)

    return {
        "statusCode": 200,
        "body": json.dumps(split_scene),
    }
