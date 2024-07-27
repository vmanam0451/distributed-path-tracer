from pygltflib import *
import boto3

class Preprocesser:
    def __init__(self, scene_bucket, memory_per_worker, num_workers = None):
        self.memory_per_worker = memory_per_worker
        self.num_workers = num_workers
        self.scene_location = scene_bucket
        
        s3 = boto3.client('s3')
        s3.download_file(scene_bucket, 'scene.gltf', 'scene.gltf')
        self.gltf = GLTF2().load('scene.gltf')
    
    def get_split_scene(self):
        current_worker_id = 1
        scene: Scene = self.gltf.scenes[0]

        node_idx: Optional[int]
        for node_idx in scene.nodes:
            if node_idx is None: continue
            node: Node = self.gltf.nodes[node_idx]
            
            if node.mesh is None: continue            
                
            mesh: Mesh = self.gltf.meshes[node.mesh]
            for primitive in mesh.primitives: print(self.get_primitive_size(primitive))
            
    def get_primitive_size(self, primitive: Primitive):
        # Buffer size
        buffer_size, material_size = 0, 0
        
        get_attribute_size = lambda attr: 0 if attr is None else self.gltf.bufferViews[attr].byteLength
        attribute: Attributes = primitive.attributes
    
        pos_length =  get_attribute_size(attribute.POSITION)
        normal_length = get_attribute_size(attribute.NORMAL)
        tangent_length = get_attribute_size(attribute.TANGENT)
        texcoord_length = get_attribute_size(attribute.TEXCOORD_0)
        # For now, we only consider the first texture coordinate. Mainly because none of the scenes I'm using have more than one texture coordinate

        buffer_size += pos_length + normal_length + tangent_length + texcoord_length
        
        get_material_size = lambda texture: self.get_texture_size(texture)
        material: Material = self.gltf.materials[primitive.material]
        
        normal_tex_size = get_material_size(material.normalTexture)
        occlusion_tex_size = get_material_size(material.occlusionTexture)
        emissive_tex_size = get_material_size(material.emissiveTexture)
        base_color_tex_size = get_material_size(material.pbrMetallicRoughness.baseColorTexture)
        metallic_roughness_tex_size = get_material_size(material.pbrMetallicRoughness.metallicRoughnessTexture) 
        
        material_size += normal_tex_size + occlusion_tex_size + emissive_tex_size + base_color_tex_size + metallic_roughness_tex_size
        
        return buffer_size + material_size
    
    def get_texture_size(self, texture: Optional[Texture]):
        if texture is None:
            return 0
        
        image: Image = self.gltf.images[texture.source]
        
        s3 = boto3.client('s3')
        response = s3.head_object(Bucket=self.scene_location, Key=image.uri)
        return response['ContentLength']

        