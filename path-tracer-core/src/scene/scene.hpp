#pragma once


#include "models/work_info.hpp"
#include <pch.hpp>
#include <path_tracer/scene/entity.hpp>
#include <path_tracer/image/texture.hpp>
#include <path_tracer/scene/camera.hpp>
#include <path_tracer/scene/model.hpp>
#include <path_tracer/scene/sun_light.hpp>
#include <path_tracer/core/mesh.hpp>
#include <path_tracer/core/material.hpp>

namespace cloud {
    class distributed_scene {
    public:
        distributed_scene();
        ~distributed_scene();

        static constexpr uint32_t no_sun_light = static_cast<uint32_t>(-1);

        void load_scene(const std::filesystem::path& gltf_path);

    private:
        void process_node(cgltf_node* cgltf_node, cgltf_camera* cgltf_camera, cgltf_light* cgltf_sun_light, scene::entity* parent, const std::filesystem::path& gltf_path);
        std::shared_ptr<core::mesh> get_mesh(cgltf_primitive* primitive, const std::filesystem::path& gltf_path);
		std::shared_ptr<core::material>  get_material(cgltf_primitive* primitive);

    private:
        cgltf_data* data;

        std::string scene_s3_bucket;
        std::map<mesh_name, primitives> scene_work;
        
        std::unordered_map<std::string, std::shared_ptr<scene::entity>> entities;
        std::shared_ptr<scene::entity> camera;
		std::shared_ptr<scene::entity> sun_light;
		std::shared_ptr<image::texture> environment;

        uint32_t camera_index = 0;
        uint32_t sun_light_index = 0;
    };
}