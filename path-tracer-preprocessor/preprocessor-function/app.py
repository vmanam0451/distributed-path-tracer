import json
import boto3
from typing import List
import yaml
import os

from preprocess.preprocessor import Preprocessor

def create_topic(sns_client, topic_name: str):
    try:
        response = sns_client.create_topic(Name=topic_name)
        return response
    except Exception as e:
        print("Error creating topic: {}".format(e))
        
def create_queues(sns_client, sqs_client, topic_arn, scene_name, worker_ids: List[int]):
    def create_and_subscribe_worker_queue(worker_id: str) -> str:
        try:
            response = sqs_client.create_queue(QueueName='{}-worker-{}'.format(scene_name, worker_id))
            queue_url = response['QueueUrl']
            queue_attributes = sqs_client.get_queue_attributes(QueueUrl=queue_url, AttributeNames=['QueueArn'])
            queue_arn = queue_attributes['Attributes']['QueueArn']
            
            filter_policy = {'worker_id': [worker_id]} if worker_id == 'master' else {"worker_id" : [worker_id, "ALL"]}
            sns_client.subscribe(
                TopicArn=topic_arn,
                Protocol='sqs',
                Endpoint=queue_arn,
                Attributes={
                    'FilterPolicy': json.dumps(filter_policy)
                }
            )
            
            queue_policy = {
                "Statement": [
                    {
                        "Effect": "Allow",
                        "Principal": { "Service": "sns.amazonaws.com" },
                        "Action": "sqs:SendMessage",
                        "Resource": queue_arn,
                        "Condition": {
                            "ArnEquals": {
                                "aws:SourceArn": topic_arn
                            }
                        }
                    }
                ]
            }
            
            sqs_client.set_queue_attributes(
                QueueUrl=queue_url,
                Attributes={'Policy': json.dumps(queue_policy)}
            )
            
            return queue_url
        except Exception as e:
            print("Error creating queue: {}".format(e))
        
    worker_queues = {}
    
    master_queue_arn = create_and_subscribe_worker_queue('master')
    worker_queues['master'] = master_queue_arn
        
    try:   
        for worker_id in worker_ids:
            queue_arn = create_and_subscribe_worker_queue(str(worker_id))
            worker_queues[worker_id] = queue_arn
                    
        return worker_queues
    
    except Exception as e:
        print("Error creating queue: {}".format(e))

def lambda_handler(event, context):
    function_input = event['body']
    
    scene_bucket = function_input['scene_bucket']
    scene_key = function_input['scene_key']
    scene_name = function_input['scene_name']
    
    config_file = "prod.yml" 
    
    with open(config_file, 'r') as stream:
        config = yaml.safe_load(stream)

    preprocessor = Preprocessor(scene_bucket=scene_bucket, scene_root=scene_key)    
    split_scene = preprocessor.get_split_scene()
    
    session = boto3.session.Session()
    
    sns_client = session.client(
        service_name='sns',
        region_name=config['sns']['region'],
    )
    
    
    sqs_client = boto3.client(
        service_name='sqs',
        region_name=config['sqs']['region'],
    )
        
    sns_response = create_topic(sns_client, '{}-topic'.format(scene_name))
    print(sns_response)
    
    topic_arn = sns_response['TopicArn']

    
    worker_queues = create_queues(sns_client, sqs_client, topic_arn, scene_name, split_scene['split_work'].keys())
    
    output = {
        "scene": split_scene,
        "queues": worker_queues
    }
    
    return {
        "statusCode": 200,
        "body": json.dumps(output),
    }
