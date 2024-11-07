
#include "scene.hpp"

namespace cloud {
    distributed_scene::~distributed_scene() {
        cgltf_free(data);
    }
}