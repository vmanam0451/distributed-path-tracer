from pygltflib import *

class Preprocesser:
    def __init__(self, gltf_file, memory_per_worker, num_workers = None):
        self.memory_per_worker = memory_per_worker
        self.num_workers = num_workers
        self.gltf = GLTF2().load(gltf_file)

    
    def get_split_scene(self):
        current_worker_id = 1
        scene = self.gltf.scenes[0]

        for node in scene.nodes:
            pass

    def get_primitive_size(self, primitive: Primitive):
        # Buffer size
        buffer_size, material_size = 0, 0
        
        attribute: Attributes = primitive.attributes
    
        pos_index: int = attribute.POSITION
        normal_index: int = attribute.NORMAL
        tangent_index: int = attribute.TANGENT
        texcoord_0_index: int = attribute.TEXCOORD_0
        # For now, we only consider the first texture coordinate. Mainly because none of the scenes I'm using have more than one texture coordinate

        pos_length =  self.gltf.bufferViews[pos_index].byteLength
        normal_length = self.gltf.bufferViews[normal_index].byteLength
        tangent_length = self.gltf.bufferViews[tangent_index].byteLength
        texcoord_length = self.gltf.bufferViews[texcoord_0_index].byteLength

        buffer_size += pos_length + normal_length + tangent_length + texcoord_length
        
        material: Material = self.gltf.materials[primitive.material]
        
        pass
    

        