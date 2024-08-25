#include "scene.hpp"
#include <stdexcept>

namespace scene {
    distributed_scene::~distributed_scene() {
        cgltf_free(data);
    }

    void distributed_scene::load_scene(const std::filesystem::path& gltf_path) {
        cgltf_options options = {};
		cgltf_result result = cgltf_parse_file(&options, gltf_path.string().c_str(), &data);
		if (result == cgltf_result_success) {
            spdlog::info("Loaded glTF file: {}", gltf_path.string());
		}
		else {
            spdlog::error("Failed to load glTF file: {}", gltf_path.string());
            throw std::runtime_error("Failed to load glTF file");
		}
    }
}