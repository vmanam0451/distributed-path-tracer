#pragma once

#include <pch.hpp>

#include <path_tracer/scene/entity.hpp>

namespace scene {
    class distributed_scene {
    public:
        distributed_scene();
        ~distributed_scene();

        void load_scene(const std::filesystem::path& gltf_path);

    private:
        cgltf_data* data;
    };
}