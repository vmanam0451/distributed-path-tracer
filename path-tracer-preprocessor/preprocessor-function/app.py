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
    """
    Split the image into horizontal strips, one per worker.
    This ensures all pixels are covered regardless of num_workers.
    """
    workers = {}
    
    for worker_id in range(1, num_workers + 1):
        y_start = (height * (worker_id - 1)) // num_workers
        y_end = (height * worker_id) // num_workers - 1
        
        workers[worker_id] = {
            "minX": 0,
            "maxX": width - 1,
            "minY": y_start,
            "maxY": y_end
        }
    
    return workers

def lambda_handler(event, context):
    try:
        function_input = json.loads(event['body'])
        scene_bucket = function_input['scene_bucket']
        scene_key = function_input['scene_key']
        num_workers = function_input['num_workers']
        samples = function_input.get('samples', 50)
        bounces = function_input.get('bounces', 5)
        X = function_input.get('X', 640)
        Y = function_input.get('Y', 480)
        cloud_map_namespace = function_input.get('cloud_map_namespace')
        cloud_map_service = function_input.get('cloud_map_service')
        cloud_map_service_id = function_input.get('cloud_map_service_id')
        results_queue = function_input.get('results_queue_url')
    
        preprocessor = Preprocessor(scene_bucket=scene_bucket, scene_root=scene_key, num_workers=num_workers)    
        split_scene = preprocessor.get_split_scene()
        print("Completed splitting the scene")
        print("Split Scene: {}".format(split_scene))

        session = boto3.session.Session()
        AWS_REGION = session.region_name       

        worker_infos = {}
        sub_grid = split_2d_grid_rectangular(X, Y, num_workers)
        print("Sub grid for workers: {}".format(sub_grid))

        for worker_id in split_scene['split_work'].keys():
            worker_info = {
                "scene_info": split_scene['split_work'][worker_id],
                "scene_bucket": scene_bucket,
                "scene_root": scene_key,
                "worker_id": str(worker_id),
                "num_workers": len(split_scene['split_work'].keys()),
                "samples": samples,
                "bounces": bounces,
                "min_x": sub_grid[worker_id]["minX"],
                "max_x": sub_grid[worker_id]["maxX"],
                "min_y": sub_grid[worker_id]["minY"],
                "max_y": sub_grid[worker_id]["maxY"],
                "image_width": X,
                "image_height": Y,
                "cloud_map_namespace": cloud_map_namespace,
                "cloud_map_service": cloud_map_service,
                "cloud_map_service_id": cloud_map_service_id,
                "results_queue_url": "",
                "aws_region": AWS_REGION
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
            "num_workers": len(split_scene['split_work'].keys()),
            "samples": samples,
            "bounces": bounces,
            "min_x": 0,
            "max_x": X,
            "min_y": 0,
            "max_y": Y,
            "image_width": X,
            "image_height": Y,
            "cloud_map_namespace": cloud_map_namespace,
            "cloud_map_service": cloud_map_service,
            "cloud_map_service_id": cloud_map_service_id,
            "results_queue_url": results_queue,
            "aws_region": AWS_REGION
        }
        
        for worker_id, worker_info in worker_infos.items():
            print(f"Launching Fargate task for worker id: {worker_id}")
            scene_size_mb = int(worker_info['scene_info'].get('total_size', 0) * 1024)
            
            base_memory = 16384
            memory = base_memory # For testing, set all to 16 GB
            cpu = 8192        # Default CPU 8 vCPU
            
            if worker_id == 'master':
                memory = 8192  
                cpu = 4096    
                
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
        
        if response.get('failures'):
            for failure in response['failures']:
                print(f"Task launch failure for worker {worker_info['worker_id']}: "
                      f"reason={failure.get('reason')}, detail={failure.get('detail')}")
        
        task_arn = response['tasks'][0]['taskArn'] if response['tasks'] else None
        print(f"Launched task {task_arn} for worker {worker_info['worker_id']}")
        return task_arn
    
    except Exception as e:
        print(f"Error launching Fargate task: {e}")
        traceback.print_exc()
        return None