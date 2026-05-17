#include <filesystem>
#include <path_tracer/image/image_texture.hpp>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "cloud/s3.hpp"
#include "scene.hpp"

namespace cloud
{

// Build a scene::transform that exactly reproduces a column-major 4x4
// world matrix. Because geometry entities created by the per-instance
// loader have no parent, this local transform IS their global
// transform — no TRS decomposition required.
static scene::transform transform_from_world_matrix(const std::array<float, 16> &m)
{
    math::fvec3 origin(m[12], m[13], m[14]);
    math::fmat3 basis(math::fvec3(m[0], m[1], m[2]),  // column 0
                      math::fvec3(m[4], m[5], m[6]),  // column 1
                      math::fvec3(m[8], m[9], m[10])); // column 2
    return scene::transform(origin, basis);
}

// Apply the preprocessor's "mesh_<index>" fallback so that unnamed
// meshes can still be looked up by the worker. The Go preprocessor
// uses the same rule (services.CollectPrimitives), so both sides
// agree on the wire-side mesh name.
static std::string effective_mesh_name(cgltf_mesh *mesh, size_t index)
{
    if (mesh->name && *mesh->name != '\0')
        return std::string(mesh->name);
    return "mesh_" + std::to_string(index);
}

void distributed_scene::load_scene(const std::string &scene_s3_bucket, const std::string &scene_s3_root,
                                   const std::vector<models::scene_instance> &instances,
                                   const std::filesystem::path &gltf_path)
{
    this->m_scene_s3_bucket = scene_s3_bucket;
    this->m_scene_s3_root = scene_s3_root;
    this->scene_instances = instances;

    uint32_t camera_index = 0;
    uint32_t sun_light_index = 0;
    uint32_t no_sun_light = static_cast<uint32_t>(-1);

    cgltf_options options = {};
    cgltf_result result = cgltf_parse_file(&options, gltf_path.string().c_str(), &m_data);
    if (result == cgltf_result_success)
    {
        spdlog::info("Loaded glTF file: {}", gltf_path.string());
    }
    else
    {
        spdlog::error("Failed to load glTF file: {}", gltf_path.string());
        throw std::runtime_error("Failed to load glTF file");
    }

    cgltf_scene main_scene = m_data->scenes[0];

    if (m_data->cameras_count < camera_index + 1)
        throw std::runtime_error("Scene does not contain camera #" + util::to_string(camera_index) + ".");

    cgltf_camera *cgltf_camera = &m_data->cameras[camera_index];

    cgltf_light *cgltf_sun_light = nullptr;
    if (sun_light_index != no_sun_light)
    {
        if (m_data->lights_count < sun_light_index + 1)
        {
            spdlog::error("Scene does not contain sun light #{} No sun light will be used.", sun_light_index);
        }
        else if (m_data->lights[sun_light_index].type != cgltf_light_type_directional)
        {
            spdlog::error("Light #{} is not a sun light. No sun light will be used.", sun_light_index);
        }
        else
        {
            cgltf_sun_light = &m_data->lights[sun_light_index];
        }
    }

    // Walk the node graph only for camera + lights. Geometry placement
    // is driven by the instance list further down.
    for (int i = 0; i < main_scene.nodes_count; i++)
    {
        process_node(main_scene.nodes[i], cgltf_camera, cgltf_sun_light, nullptr, gltf_path);
    }

    if (!m_camera)
        throw std::runtime_error("Scene is missing a camera.");

    // Build a (effective name -> cgltf_mesh*) lookup so we can match
    // up instance entries with their underlying glTF mesh.
    std::unordered_map<std::string, cgltf_mesh *> mesh_by_name;
    mesh_by_name.reserve(m_data->meshes_count);
    for (size_t i = 0; i < m_data->meshes_count; i++)
    {
        cgltf_mesh *mesh = &m_data->meshes[i];
        mesh_by_name[effective_mesh_name(mesh, i)] = mesh;
    }

    // Per-(mesh, prim) cache so two instances of the same primitive
    // on this worker share one parsed core::mesh and core::material.
    std::map<std::pair<std::string, int>, std::shared_ptr<core::mesh>> mesh_cache;
    std::map<std::pair<std::string, int>, std::shared_ptr<core::material>> material_cache;

    for (size_t inst_idx = 0; inst_idx < scene_instances.size(); inst_idx++)
    {
        const auto &inst = scene_instances[inst_idx];

        auto it = mesh_by_name.find(inst.mesh_name);
        if (it == mesh_by_name.end())
        {
            spdlog::warn("Instance {} references unknown mesh '{}'", inst_idx, inst.mesh_name);
            continue;
        }
        cgltf_mesh *cmesh = it->second;
        if (inst.prim_idx < 0 || static_cast<size_t>(inst.prim_idx) >= cmesh->primitives_count)
        {
            spdlog::warn("Instance {} references invalid prim_idx {} on mesh '{}'", inst_idx, inst.prim_idx,
                         inst.mesh_name);
            continue;
        }
        cgltf_primitive *primitive = cmesh->primitives + inst.prim_idx;

        auto key = std::make_pair(inst.mesh_name, inst.prim_idx);

        std::shared_ptr<core::mesh> mesh;
        if (auto mit = mesh_cache.find(key); mit != mesh_cache.end())
        {
            mesh = mit->second;
        }
        else
        {
            mesh = get_mesh(primitive, gltf_path);
            mesh_cache[key] = mesh;
        }

        std::shared_ptr<core::material> material;
        if (auto matit = material_cache.find(key); matit != material_cache.end())
        {
            material = matit->second;
        }
        else
        {
            material = get_material(primitive);
            material_cache[key] = material;
        }

        auto entity = std::make_shared<scene::entity>();
        // Name is informational only — instance entities live in
        // m_instance_entities, not in the name-keyed m_entities map.
        entity->set_name(inst.mesh_name + "#" + std::to_string(inst.prim_idx) + "@" + std::to_string(inst_idx));
        entity->set_local_transform(transform_from_world_matrix(inst.world_matrix));

        auto model = entity->add_component<scene::model>();
        model->surfaces.push_back({mesh, material});
        model->recalculate_aabb();

        m_instance_entities.push_back(entity);
    }

    cgltf_free(m_data);
    m_data = nullptr;

    m_texture_cache.clear();
    m_buffers_loaded.clear();
}

// process_node walks the node hierarchy to assemble entities for the
// camera and sun light, preserving the parent chain so that nested
// camera/light transforms compose correctly. Mesh loading is no
// longer done here — geometry is loaded from the explicit instance
// list in load_scene using pre-baked world matrices.
//
// The full hierarchy is retained (top-level entities go into
// m_entities, descendants live as children of their parent) so the
// camera's get_global_transform() can walk up to its ancestors.
void distributed_scene::process_node(cgltf_node *cgltf_node, cgltf_camera *cgltf_camera, cgltf_light *cgltf_sun_light,
                                     scene::entity *parent, const std::filesystem::path &gltf_path)
{
    auto entity = std::make_shared<scene::entity>();

    if (cgltf_node->camera)
        entity->set_name(cgltf_node->camera->name);
    else if (cgltf_node->light)
        entity->set_name(cgltf_node->light->name);
    else
        entity->set_name(cgltf_node->name ? cgltf_node->name : "");

    float local_transform[16];
    cgltf_node_transform_local(cgltf_node, local_transform);

    cgltf_float *cgltf_rotation = cgltf_node->has_rotation ? cgltf_node->rotation : nullptr;
    math::quat rotation;
    if (cgltf_rotation)
        rotation = math::quat(cgltf_rotation[3], cgltf_rotation[0], cgltf_rotation[1], cgltf_rotation[2]);

    cgltf_float *cgltf_scale = cgltf_node->has_scale ? cgltf_node->scale : nullptr;
    math::fvec3 scale = cgltf_scale ? math::fvec3(cgltf_scale[0], cgltf_scale[1], cgltf_scale[2]) : math::fvec3::one;

    cgltf_float *cgltf_translation = cgltf_node->has_translation ? cgltf_node->translation : nullptr;
    math::fvec3 translation = cgltf_translation
                                  ? math::fvec3(cgltf_translation[0], cgltf_translation[1], cgltf_translation[2])
                                  : math::fvec3::zero;

    entity->set_local_transform(scene::transform::make(translation, rotation, scale));

    // (Geometry no longer attached here. The per-instance loader in
    // load_scene handles that using pre-baked world matrices.)

    if (cgltf_camera && cgltf_camera->name && entity->get_name() == std::string(cgltf_camera->name))
    {
        auto camera = entity->add_component<scene::camera>();

        this->m_camera = entity;

        float vfov = cgltf_camera->data.perspective.yfov;
        camera->set_fov(vfov);
    }

    if (cgltf_sun_light && cgltf_sun_light->name && entity->get_name() == std::string(cgltf_sun_light->name))
    {
        auto sun_light = entity->add_component<scene::sun_light>();

        this->m_sun_light = entity;

        sun_light->energy =
            math::fvec3(cgltf_sun_light->color[0], cgltf_sun_light->color[1], cgltf_sun_light->color[2]) *
            cgltf_sun_light->intensity;
    }

    if (cgltf_node->children_count > 0)
    {
        for (int i = 0; i < cgltf_node->children_count; i++)
            process_node(cgltf_node->children[i], cgltf_camera, cgltf_sun_light, entity.get(), gltf_path);
    }

    if (parent)
        entity->set_parent(parent->shared_from_this());
    else
        m_entities[entity->get_name()] = entity;

    spdlog::info("Loaded: {}", entity->get_name());
}

std::shared_ptr<image::texture> distributed_scene::get_cached_texture(const std::string &scene_bucket,
                                                                      const std::string &image_key, bool srgb)
{
    if (m_texture_cache.contains(image_key))
    {
        auto texture = m_texture_cache[image_key].lock();
        if (texture)
            return texture;
    }

    std::variant<std::filesystem::path, std::vector<uint8_t>> output{std::vector<uint8_t>{}};
    cloud::s3_download_object(scene_bucket, image_key, output);

    auto texture = image::image_texture::load_from_memory(std::get<1>(output), srgb);
    m_texture_cache[image_key] = texture;
    return texture;
}

bool distributed_scene::is_buffer_loaded(const std::string &uri)
{
    if (m_buffers_loaded.contains(uri))
        return true;

    m_buffers_loaded.insert(uri);
    return false;
}

std::shared_ptr<core::mesh> distributed_scene::get_mesh(cgltf_primitive *primitive,
                                                        const std::filesystem::path &gltf_path)
{
    std::shared_ptr<core::mesh> mesh = std::make_shared<core::mesh>();

    std::vector<float> positions;
    std::vector<float> tex_coords;
    std::vector<float> normals;
    std::vector<float> tangents;

    for (int i = 0; i < primitive->attributes_count; i++)
    {
        cgltf_attribute *attribute = primitive->attributes + i;
        cgltf_buffer *buffer = attribute->data->buffer_view->buffer;

        std::string buffer_uri = buffer->uri;

        bool buffer_loaded = is_buffer_loaded(buffer_uri);
        if (!buffer_loaded)
        {
            std::filesystem::path path = std::filesystem::path("/tmp/" + buffer_uri);
            std::variant<std::filesystem::path, std::vector<uint8_t>> output{path};
            cloud::s3_download_object(this->m_scene_s3_bucket, this->m_scene_s3_root + buffer_uri, output);

            cgltf_options options = {};
            cgltf_load_buffer_file(&options, buffer->size, buffer_uri.c_str(), gltf_path.string().c_str(),
                                   &buffer->data);
        }

        cgltf_size count = attribute->data->count;
        cgltf_accessor *accessor = attribute->data;

        if (attribute->type == cgltf_attribute_type_position)
        {
            positions.resize(count * 3);
            cgltf_accessor_unpack_floats(accessor, positions.data(), count * 3);
        }

        else if (attribute->type == cgltf_attribute_type_texcoord)
        {
            tex_coords.resize(count * 2);
            cgltf_accessor_unpack_floats(accessor, tex_coords.data(), count * 2);
        }

        else if (attribute->type == cgltf_attribute_type_normal)
        {
            normals.resize(count * 3);
            cgltf_accessor_unpack_floats(accessor, normals.data(), count * 3);
        }

        else if (attribute->type == cgltf_attribute_type_tangent)
        {
            tangents.resize(count * 3);
            cgltf_accessor_unpack_floats(accessor, tangents.data(), count * 3);
        }
    }

    std::vector<unsigned int> indices(primitive->indices->count);
    cgltf_accessor_unpack_indices(primitive->indices, indices.data(), sizeof(unsigned int), primitive->indices->count);

    auto vertices_size = positions.size() / 3;
    auto trianges_size = indices.size() / 3;

    const uint8_t VEC_3_STRIDE = 3;
    const uint8_t VEC_2_STRIDE = 2;

    mesh->vertices.resize(vertices_size);
    mesh->triangles.resize(trianges_size);

    for (size_t i = 0, j = 0, k = 0; k < vertices_size; i += VEC_3_STRIDE, j += VEC_2_STRIDE, k++)
    {
        core::vertex &v = mesh->vertices[k];

        v.position.x = positions[i];
        v.position.y = positions[i + 1];
        v.position.z = positions[i + 2];

        v.tex_coord.x = tex_coords[j + 0];
        v.tex_coord.y = tex_coords[j + 1];

        v.normal.x = normals[i];
        v.normal.y = normals[i + 1];
        v.normal.z = normals[i + 2];

        v.tangent.x = tangents[i];
        v.tangent.y = tangents[i + 1];
        v.tangent.z = tangents[i + 2];
    }

    for (size_t i = 0, j = 0; i < trianges_size; i++, j += VEC_3_STRIDE)
    {
        mesh->triangles[i].x = indices[j];
        mesh->triangles[i].y = indices[j + 1];
        mesh->triangles[i].z = indices[j + 2];
    }

    mesh->recalculate_aabb();
    mesh->build_kd_tree();

    return mesh;
}

std::shared_ptr<core::material> distributed_scene::get_material(cgltf_primitive *primitive)
{
    cgltf_material *cgltf_material = primitive->material;
    auto material = std::make_shared<core::material>();

    if (!cgltf_material)
        return material;

    cgltf_float *cgltf_albedo_opacity = cgltf_material->pbr_metallic_roughness.base_color_factor; // float[4]
    cgltf_float cgltf_roughness = cgltf_material->pbr_metallic_roughness.roughness_factor;
    cgltf_float cgltf_metallic = cgltf_material->pbr_metallic_roughness.metallic_factor;
    cgltf_float *cgltf_emissive = cgltf_material->emissive_factor; // float[3]
    cgltf_alpha_mode cgltf_alpha_mode = cgltf_material->alpha_mode;

    auto cgltf_normal_texture = cgltf_material->normal_texture;
    auto cgltf_albedo_opacity_texture = cgltf_material->pbr_metallic_roughness.base_color_texture;
    auto cgltf_occlusion_texture = cgltf_material->occlusion_texture;
    auto cgltf_roughness_metallic_texture = cgltf_material->pbr_metallic_roughness.metallic_roughness_texture;
    auto cgltf_emissive_texture = cgltf_material->emissive_texture;

    std::string cgltf_normal_tex_path = cgltf_normal_texture.texture ? cgltf_normal_texture.texture->image->uri : "";
    std::string cgltf_albedo_opacity_tex_path =
        cgltf_albedo_opacity_texture.texture ? cgltf_albedo_opacity_texture.texture->image->uri : "";
    std::string cgltf_occlusion_tex_path =
        cgltf_occlusion_texture.texture ? cgltf_occlusion_texture.texture->image->uri : "";
    std::string cgltf_roughness_metallic_tex_path =
        cgltf_roughness_metallic_texture.texture ? cgltf_roughness_metallic_texture.texture->image->uri : "";
    std::string cgltf_emissive_tex_path =
        cgltf_emissive_texture.texture ? cgltf_emissive_texture.texture->image->uri : "";

    material->albedo_fac = math::fvec3(cgltf_albedo_opacity[0], cgltf_albedo_opacity[1], cgltf_albedo_opacity[2]);
    material->opacity_fac = cgltf_albedo_opacity[3];
    material->roughness_fac = cgltf_roughness;
    material->metallic_fac = cgltf_metallic;
    material->emissive_fac = math::fvec3(cgltf_emissive[0], cgltf_emissive[1], cgltf_emissive[2]);

    if (!cgltf_normal_tex_path.empty())
    {
        auto normal_tex =
            get_cached_texture(this->m_scene_s3_bucket, this->m_scene_s3_root + cgltf_normal_tex_path, false);
        material->normal_tex = normal_tex;
    }

    if (!cgltf_albedo_opacity_tex_path.empty())
    {
        auto albedo_opacity_tex =
            get_cached_texture(this->m_scene_s3_bucket, this->m_scene_s3_root + cgltf_albedo_opacity_tex_path, true);
        material->albedo_tex = albedo_opacity_tex;
        if (cgltf_alpha_mode != cgltf_alpha_mode_opaque)
            material->opacity_tex = albedo_opacity_tex;
    }

    if (!cgltf_occlusion_tex_path.empty())
    {
        auto occlusion_tex =
            get_cached_texture(this->m_scene_s3_bucket, this->m_scene_s3_root + cgltf_occlusion_tex_path, false);
        material->occlusion_tex = occlusion_tex;
    }

    if (!cgltf_roughness_metallic_tex_path.empty())
    {
        auto roughness_metallic_tex = get_cached_texture(
            this->m_scene_s3_bucket, this->m_scene_s3_root + cgltf_roughness_metallic_tex_path, false);
        material->roughness_tex = roughness_metallic_tex;
        material->metallic_tex = roughness_metallic_tex;
    }

    if (!cgltf_emissive_tex_path.empty())
    {
        auto emissive_tex =
            get_cached_texture(this->m_scene_s3_bucket, this->m_scene_s3_root + cgltf_emissive_tex_path, true);
        material->emissive_tex = emissive_tex;
    }

    std::string name{cgltf_material->name};
    if (name.find("shadow") != std::string::npos && name.find("catcher") != std::string::npos)
        material->shadow_catcher = true;

    return material;
}
} // namespace cloud
