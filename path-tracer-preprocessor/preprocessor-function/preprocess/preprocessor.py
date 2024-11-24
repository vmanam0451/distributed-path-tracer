from typing import TypedDict
from pygltflib import *
import boto3

class WorkerInfo(TypedDict):
    work: Dict[str, List[int]] # mesh name -> list of primtive indices
    total_size: float
    
class SplitScene(TypedDict):
    split_work: Dict[int, WorkerInfo]
    total_size: float


class Preprocessor:
    def __init__(self, scene_bucket, scene_root, memory_per_worker_GB = None, num_workers = 1):
        self.memory_per_worker_GB = memory_per_worker_GB
        self.num_workers = num_workers
        self.scene_bucket = scene_bucket
        self.scene_root = scene_root
        
        s3 = boto3.resource('s3')
        scene_bucket = s3.Bucket(scene_bucket)
        scene_bucket.download_file(scene_root + "scene.gltf", '/tmp/scene.gltf') # Only tmp is writable in AWS Lambda
        self.gltf = GLTF2().load('/tmp/scene.gltf')
    
    def get_split_scene(self) -> SplitScene:
        current_worker_id = 1
        scene: Scene = self.gltf.scenes[0]

        node_idx: int
        total_size = 0
        current_size = 0
        total_primitives = 0
        current_primitive = 0
        split_scene: SplitScene = {'split_work': {}, 'total_size': 0}
        
        for node_idx in scene.nodes:
            node: Node = self.gltf.nodes[node_idx]
            
            if node.mesh is None: continue
            total_primitives += len(self.gltf.meshes[node.mesh].primitives)
        
        for node_idx in scene.nodes:
            node: Node = self.gltf.nodes[node_idx]
            
            if node.mesh is None: continue            
                
            mesh: Mesh = self.gltf.meshes[node.mesh]
            for prim_id, primitive in enumerate(mesh.primitives): 
                current_primitive += 1
                prim_size = self.get_primitive_size(primitive) * 1e-9
                print(prim_size)
                total_size += prim_size
                
                if current_worker_id not in split_scene['split_work']:
                    split_scene['split_work'][current_worker_id] = {"work": {}, "total_size": 0}
                        
                worker_info = split_scene['split_work'][current_worker_id]
                work = worker_info['work']
                if mesh.name not in work: work[mesh.name] = []
                work[mesh.name].append(prim_id)
                worker_info['total_size'] += prim_size
                    
                if (self.memory_per_worker_GB is not None and (current_size + prim_size) >= self.memory_per_worker_GB) or \
                    (self.num_workers is not None and (current_primitive >= total_primitives / self.num_workers)):
                    
                    current_worker_id += 1
                    current_size = 0
                    current_primitive = 0
                    
            
        print("TOTAL SIZE")
        print(total_size)
        split_scene['total_size'] = total_size
        return split_scene
            
    def get_primitive_size(self, primitive: Primitive) -> float:
        buffer_size, material_size = 0, 0
        
        get_attribute_size = lambda attr: 0 if attr is None else self.gltf.bufferViews[attr].byteLength
        attribute: Attributes = primitive.attributes
    
        pos_length =  get_attribute_size(attribute.POSITION)
        normal_length = get_attribute_size(attribute.NORMAL)
        tangent_length = get_attribute_size(attribute.TANGENT)
        texcoord_length = get_attribute_size(attribute.TEXCOORD_0)
        # For now, we only consider the first texture coordinate. Mainly because none of the scenes I'm using have more than one texture coordinate

        buffer_size += pos_length + normal_length + tangent_length + texcoord_length
        
        get_texture_size = lambda texture: 0 if texture is None else self.get_texture_size(self.gltf.textures[texture.index])
        material: Material = self.gltf.materials[primitive.material]
        
        normal_tex_size = get_texture_size(material.normalTexture)
        occlusion_tex_size = get_texture_size(material.occlusionTexture)
        emissive_tex_size = get_texture_size(material.emissiveTexture)
        base_color_tex_size = get_texture_size(material.pbrMetallicRoughness.baseColorTexture)
        metallic_roughness_tex_size = get_texture_size(material.pbrMetallicRoughness.metallicRoughnessTexture) 
        
        material_size += normal_tex_size + occlusion_tex_size + emissive_tex_size + base_color_tex_size + metallic_roughness_tex_size
        
        return buffer_size + material_size
    
    def get_texture_size(self, texture: Optional[Texture]) -> float:
        if texture is None: return 0
        
        image: Image = self.gltf.images[texture.source]
        
        s3 = boto3.client('s3')
        response = s3.head_object(Bucket=self.scene_bucket, Key=self.scene_root + image.uri)
        return response['ContentLength']

        