import json
import math
import string
import traceback
import boto3
from botocore.config import Config
from typing import List
import yaml
import os

from preprocess.preprocessor import Preprocessor

def split_2d_grid_rectangular(width, height, num_workers):
    cols = int(math.ceil(math.sqrt(num_workers)))
    rows = int(math.ceil(num_workers / cols))
    
    workers = {}
    worker_id = 1
    
    for row in range(rows):
        if worker_id > num_workers:
            break
            
        for col in range(cols):
            if worker_id > num_workers:
                break
                
            x_start = (width * col) // cols
            x_end = (width * (col + 1)) // cols - 1
            y_start = (height * row) // rows
            y_end = (height * (row + 1)) // rows - 1
            
            workers[worker_id] = {
                "minX": x_start,
                "maxX": x_end,
                "minY": y_start,
                "maxY": y_end
            }
            
            worker_id += 1
    
    return workers

def create_topic(sns_client, topic_name: str):
    try:
        # Check if topic already exists
        response = sns_client.list_topics()
        for topic in response.get('Topics', []):
            arn = topic['TopicArn']
            if arn.split(':')[-1] == topic_name:
                print(f"Topic {topic_name} already exists, using existing topic")
                return {'TopicArn': arn}
        
        response = sns_client.create_topic(Name=topic_name)
        print(f"Created new topic {topic_name}")
        return response
    except Exception as e:
        print(f"Error with SNS topic {topic_name}: {e}")
        raise
        
def create_queues(sns_client, sqs_client, topic_arn, scene_name, worker_ids: List[int]):
    def get_or_create_worker_queue(worker_id: str) -> str:
        queue_name = f'{scene_name}-distributed-scene-worker-{worker_id}'
        try:
            try:
                response = sqs_client.get_queue_url(QueueName=queue_name)
                queue_url = response['QueueUrl']
                print(f"Queue {queue_name} already exists, using existing queue")
                
                queue_attributes = sqs_client.get_queue_attributes(
                    QueueUrl=queue_url, 
                    AttributeNames=['QueueArn']
                )
                queue_arn = queue_attributes['Attributes']['QueueArn']
                
                subscriptions = sns_client.list_subscriptions_by_topic(TopicArn=topic_arn)
                already_subscribed = any(
                    sub['Endpoint'] == queue_arn 
                    for sub in subscriptions.get('Subscriptions', [])
                )
                
                if not already_subscribed:
                    _subscribe_queue_to_topic(queue_arn, worker_id)
                    _set_queue_policy(queue_url, queue_arn)
                
                return queue_url
            
            except sqs_client.exceptions.QueueDoesNotExist:
                response = sqs_client.create_queue(QueueName=queue_name)
                queue_url = response['QueueUrl']
                print(f"Created new queue {queue_name}")
                
                queue_attributes = sqs_client.get_queue_attributes(
                    QueueUrl=queue_url, 
                    AttributeNames=['QueueArn']
                )
                queue_arn = queue_attributes['Attributes']['QueueArn']
                
                _subscribe_queue_to_topic(queue_arn, worker_id)
                _set_queue_policy(queue_url, queue_arn)
                
                return queue_url
                
        except Exception as e:
            print(f"Error with SQS queue {queue_name}: {e}")
            raise
            
    def _subscribe_queue_to_topic(queue_arn, worker_id):
        if worker_id == 'master':
            filter_policy = {'worker_id': ["MASTER"]}
        else:
            filter_policy = {
                'worker_id': ['WORKERS', str(worker_id)],
                'source_worker_id': [{'anything-but': [str(worker_id)]}]
            }

        sns_client.subscribe(
            TopicArn=topic_arn,
            Protocol='sqs',
            Endpoint=queue_arn,
            Attributes={
                'FilterPolicy': json.dumps(filter_policy)
            }
        )
        print(f"Subscribed queue {queue_arn} to topic {topic_arn}")
            
    def _set_queue_policy(queue_url, queue_arn):
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
        
    try:
        worker_queues = {}
    
        master_queue_url = get_or_create_worker_queue('master')
        worker_queues['master'] = master_queue_url

        for worker_id in worker_ids:
            queue_url = get_or_create_worker_queue(str(worker_id))
            worker_queues[worker_id] = queue_url

        return worker_queues
    
    except Exception as e:
        print(f"Error creating queues: {e}")
        raise

def lambda_handler(event, context):
    try:
        function_input = json.loads(event['body'])
        scene_bucket = function_input['scene_bucket']
        scene_key = function_input['scene_key']
        scene_name = function_input['scene_name']
        num_workers = function_input['num_workers']
        samples = function_input.get('samples', 50)
        bounces = function_input.get('bounces', 5)
        X = function_input.get('X', 640)
        Y = function_input.get('Y', 480)
    
        preprocessor = Preprocessor(scene_bucket=scene_bucket, scene_root=scene_key, num_workers=num_workers)    
        split_scene = preprocessor.get_split_scene()
        print("Completed splitting the scene")
        print("Split Scene: {}".format(split_scene))

        session = boto3.session.Session()
        AWS_REGION = session.region_name

        # Always create topics and queues
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
        sub_grid = split_2d_grid_rectangular(X, Y, num_workers)
        print("Sub grid for workers: {}".format(sub_grid))

        for worker_id in split_scene['split_work'].keys():
            worker_info = {
                "scene_info": split_scene['split_work'][worker_id],
                "scene_bucket": scene_bucket,
                "scene_root": scene_key,
                "worker_id": str(worker_id),
                "sqs_queue_url": worker_queues.get(worker_id, ""),
                "sns_topic_arn": topic_arn,
                "num_workers": len(split_scene['split_work'].keys()),
                "samples": samples,
                "bounces": bounces,
                "min_x": sub_grid[worker_id]["minX"],
                "max_x": sub_grid[worker_id]["maxX"],
                "min_y": sub_grid[worker_id]["minY"],
                "max_y": sub_grid[worker_id]["maxY"]
            }
        
            worker_infos[worker_id] = worker_info
        
        worker_infos['master'] = {
            "scene_info": {
                "work": {},  
                "total_size": 0.0  
            },
            "scene_bucket": scene_bucket,
            "scene_root": scene_key,
            "worker_id": "MASTER",
            "sqs_queue_url": worker_queues.get('master', ""),
            "sns_topic_arn": topic_arn,
            "num_workers": len(split_scene['split_work'].keys()),
            "samples": samples,
            "bounces": bounces,
            "min_x": 0,
            "max_x": X,
            "min_y": 0,
            "max_y": Y
        }
        
        for worker_id, worker_info in worker_infos.items():
            print(f"Launching Fargate task for worker id: {worker_id}")
            scene_size_mb = int(worker_info['scene_info'].get('total_size', 0) * 1024)
            
            base_memory = 16384
            memory = base_memory # For testing, set all to 16 GB
            cpu = 8192    # Default CPU 8 vCPU
            
            # You can adjust resources based on worker role or scene complexity
            if worker_id == 'master':
                memory = 2048  
                cpu = 1024    
                
            task_arn = launch_fargate_task(worker_info, memory=memory, cpu=cpu)
            print(f"Launched Fargate task {task_arn} for worker id: {worker_id}")

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
    
def launch_fargate_task(worker_info, memory=4096, cpu=2048):
    try:
        cluster_name = os.environ.get('ECS_CLUSTER_ARN')
        task_definition = os.environ.get('TASK_DEFINITION_ARN')
        subnet_ids = os.environ.get('SUBNET_IDS')
        security_group_id = os.environ.get('SECURITY_GROUP_ID')
        
        if not cluster_name or not task_definition or not subnet_ids or not security_group_id:
            raise ValueError("Missing required environment variables for task launch")
            
        ecs_client = boto3.client('ecs')
        
        env_vars = [{
            'name': 'WORKER_INFO',
            'value': json.dumps(worker_info)
        }]
        
        response = ecs_client.run_task(
            cluster=cluster_name,
            taskDefinition=task_definition,
            count=1,
            launchType='FARGATE',
            networkConfiguration={
                'awsvpcConfiguration': {
                    'subnets': subnet_ids.split(","),
                    'securityGroups': [security_group_id],
                    'assignPublicIp': 'DISABLED'
                }
            },
            overrides={
                'cpu': str(cpu),
                'memory': str(memory),
                'containerOverrides': [{
                    'name': 'worker', # Container name must match with container name in ecs_stack
                    'environment': env_vars
                }]
            }
        )
        
        task_arn = response['tasks'][0]['taskArn'] if response['tasks'] else None
        print(f"Launched task {task_arn} for worker {worker_info['worker_id']}")
        return task_arn
    
    except Exception as e:
        print(f"Error launching Fargate task: {e}")
        traceback.print_exc()
        return None