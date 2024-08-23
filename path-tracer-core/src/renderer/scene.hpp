#pragma once

#include <pch.hpp>

namespace scene {
    class scene {
    public:
        scene(const std::filesystem::path& gltf_path);
        ~scene();
    };
}