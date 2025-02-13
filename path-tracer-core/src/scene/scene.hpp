#pragma once

#include <path_tracer/scene/entity.hpp>
#include <path_tracer/image/texture.hpp>
#include <path_tracer/scene/camera.hpp>
#include <path_tracer/scene/model.hpp>
#include <path_tracer/scene/sun_light.hpp>
#include <path_tracer/core/mesh.hpp>
#include <path_tracer/core/material.hpp>
#include "path_tracer/core/renderer.hpp"
#include "pch.hpp"
#include "models/cloud_ray.hpp"
#include "models/work_info.hpp"
#include "models/intersect_result.hpp"

namespace cloud {
    class distributed_scene {
    public:
        void load_scene(const std::string& scene_s3_bucket, const std::string& scene_s3_root, const std::map<mesh_name, primitives>& scene_work, const std::filesystem::path& gltf_path);
        models::intersect_result intersect(const geometry::ray& ray) const;

    private:
        void process_node(cgltf_node* cgltf_node, cgltf_camera* cgltf_camera, cgltf_light* cgltf_sun_light, scene::entity* parent, const std::filesystem::path& gltf_path);
        std::shared_ptr<core::mesh> get_mesh(cgltf_primitive* primitive, const std::filesystem::path& gltf_path);
		std::shared_ptr<core::material>  get_material(cgltf_primitive* primitive);

        std::shared_ptr<image::texture> get_cached_texture(const std::string& scene_bucket, const std::string& image_key, bool srgb);
        bool is_buffer_loaded(const std::string& uri);
        size_t get_scene_size();

    public: // TODO: Change to private
        cgltf_data* m_data{nullptr};

        std::string m_scene_s3_bucket;
        std::string m_scene_s3_root;
        std::map<mesh_name, primitives> scene_work;
        
        std::unordered_map<std::string, std::shared_ptr<scene::entity>> m_entities;
        std::shared_ptr<scene::entity> m_camera;
		std::shared_ptr<scene::entity> m_sun_light;
		std::shared_ptr<image::texture> m_environment;

        std::unordered_map<std::string, std::weak_ptr<image::texture>> m_texture_cache;
        std::unordered_set<std::string> m_buffers_loaded;
    };
}