import json
import string
import traceback
import boto3
from botocore.config import Config
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
            response = sqs_client.create_queue(QueueName='{}-distributed-scene-worker-{}'.format(scene_name, worker_id))
            queue_url = response['QueueUrl']
            queue_attributes = sqs_client.get_queue_attributes(QueueUrl=queue_url, AttributeNames=['QueueArn'])
            queue_arn = queue_attributes['Attributes']['QueueArn']
            
            filter_policy = {'worker_id': [worker_id]} if worker_id == 'master' else {"worker_id" : ["ALL"]}
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
        
    try:
        worker_queues = {}
    
        master_queue_arn = create_and_subscribe_worker_queue('master')
        worker_queues['master'] = master_queue_arn
          
        for worker_id in worker_ids:
            queue_arn = create_and_subscribe_worker_queue(str(worker_id))
            worker_queues[worker_id] = queue_arn
                    
        return worker_queues
    
    except Exception as e:
        print("Error creating queue: {}".format(e))

def lambda_handler(event, context):
    try:
        function_input = json.loads(event['body'])
        scene_bucket = function_input['scene_bucket']
        scene_key = function_input['scene_key']
        scene_name = function_input['scene_name']
        num_workers = function_input['num_workers']
    
        ENVIRONMENT = os.environ.get('ENV', 'local')
        print("ENVIRONMENT: {}".format(ENVIRONMENT))
        
        preprocessor = Preprocessor(scene_bucket=scene_bucket, scene_root=scene_key, num_workers=num_workers)    
        split_scene = preprocessor.get_split_scene()
        print("Completed splitting the scene scene")
    
        session = boto3.session.Session()
        AWS_REGION = session.region_name

        topic_arn = ""
        worker_queues = {}

        if ENVIRONMENT != 'local': # TODO: Can create queues in local as well. Can locally invoke PathTraceFunction, and workers can communicate through SNS/SQS
            print("Creating topic and queues...")
            sns_client = session.client(
                service_name='sns',
                region_name=AWS_REGION,
            )
    
            sqs_client = boto3.client(
                service_name='sqs',
                region_name=AWS_REGION,
            )   
        
            sns_response = create_topic(sns_client, '{}-distributed-scene-topic'.format(scene_name))
            topic_arn = sns_response['TopicArn']
            worker_queues = create_queues(sns_client, sqs_client, topic_arn, scene_name, split_scene['split_work'].keys())

            print("Created topic and queues")
    
        worker_infos = {}
    
        for worker_id in split_scene['split_work'].keys():
            worker_info = {
                "scene_info": split_scene['split_work'][worker_id],
                "scene_bucket": scene_bucket,
                "scene_root": scene_key,
                "worker_id": str(worker_id),
                "sqs_queue_arn": worker_queues.get(worker_id, ""),
                "sns_topic_arn": topic_arn
            }
        
            worker_infos[worker_id] = worker_info
        
        for worker_id, worker_info in worker_infos.items():
            if ENVIRONMENT != 'local':
                print("Invoking Path Trace Function for worker id: {}".format(worker_id))
                lambda_client = boto3.client('lambda',)
                lambda_client.invoke(
                    FunctionName="distributed-path-tracer-worker", # specified in path-tracer.yaml
                    InvocationType='Event',
                    Payload=json.dumps(worker_info),
                )
                print("Invoked Path Trace Function for worker id: {}".format(worker_id))

        output = {
            "worker_infos": worker_infos
        }
    
        return {
            "statusCode": 200,
            "body": json.dumps(output),
        }
    
    except Exception as e:
        return {
            "statusCode": 500,
            "body": json.dumps({"error": traceback.format_exc()}),  
        }   