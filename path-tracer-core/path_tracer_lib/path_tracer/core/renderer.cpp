#include "path_tracer/core/renderer.hpp"

#include "path_tracer/core/material.hpp"
#include "path_tracer/core/mesh.hpp"
#include "path_tracer/core/pbr.hpp"
#include "path_tracer/geometry/ray.hpp"
#include "path_tracer/image/image.hpp"
#include "path_tracer/image/image_texture.hpp"
#include "path_tracer/math/vec2.hpp"
#include "path_tracer/math/vec3.hpp"
#include "path_tracer/math/mat3.hpp"
#include "path_tracer/math/math.hpp"
#include "path_tracer/scene/camera.hpp"
#include "path_tracer/scene/entity.hpp"
#include "path_tracer/scene/model.hpp"
#include "path_tracer/scene/sun_light.hpp"
#include "path_tracer/scene/transform.hpp"
#include "path_tracer/util/rand_cone_vec.hpp"
#include "path_tracer/util/thread_pool.hpp"
#include "utils.hpp"

using namespace geometry;
using namespace math;
using namespace scene;

namespace core {
	renderer::~renderer() {
		if (data)
			cgltf_free(data);
	}

	
	static std::shared_ptr<image::texture> get_cached_texture(const std::filesystem::path& path, bool srgb) {
		static std::unordered_map<std::string, std::weak_ptr<image::texture>> texture_cache;

		if (path.empty())
			return nullptr;

		std::string path_str = path.string();
		path_str = std::regex_replace(path_str, std::regex("%20"), " "); // GLTF encodes spaces as %20

		if (texture_cache.contains(path_str)) {
			auto texture = texture_cache[path_str].lock();
			if (texture)
				return texture;
		}

		auto texture = image::image_texture::load(path_str, srgb);
		texture_cache[path_str] = texture;
		return texture;
	}

	static bool is_buffer_loaded(const std::string& uri) {
		static std::unordered_set<std::string> buffers_loaded;
		if (buffers_loaded.contains(uri)) return true;

		buffers_loaded.insert(uri);
		return false;
	}

	void renderer::load_gltf(const std::filesystem::path& path) {
		cgltf_options options = {};
		cgltf_result result = cgltf_parse_file(&options, path.string().c_str(), &data);
		if (result == cgltf_result_success) {
			std::cout << "Loaded GLTF scene: " << path << std::endl;
		}
		else {
			std::cerr << "Failed to load GLTF scene: " << path << std::endl;
		}

		cgltf_scene main_scene = data->scenes[0];

		if (data->cameras_count < camera_index + 1)
			throw std::runtime_error("Scene does not contain camera #" + util::to_string(camera_index) + ".");

		cgltf_camera* cgltf_camera = &data->cameras[camera_index];

		cgltf_light* cgltf_sun_light = nullptr;
		if (sun_light_index != renderer::no_sun_light) {
			if (data->lights_count < sun_light_index + 1) {
				std::cerr << "Scene does not contain sun light #" << sun_light_index << ". No sun light will be used."
					<< std::endl;
			}
			else if (data->lights[sun_light_index].type != cgltf_light_type_directional) {
				std::cerr << "Light #" << sun_light_index << " is not a sun light. No sun light will be used." <<
					std::endl;
			}
			else {
				cgltf_sun_light = &data->lights[sun_light_index];
			}
		}

		for (int i = 0; i < main_scene.nodes_count; i++) {
			process_node(main_scene.nodes[i], cgltf_camera, cgltf_sun_light, nullptr, path);
		}

		if (!camera)
			throw std::runtime_error("Scene is missing a camera.");
	}

	void renderer::process_node(cgltf_node* cgltf_node, cgltf_camera* cgltf_camera, cgltf_light* cgltf_sun_light, entity* parent, const std::filesystem::path& path) {
		// Set Properties

		auto entity = std::make_shared<scene::entity>();

		if (cgltf_node->camera)
			entity->set_name(cgltf_node->camera->name);
		else if (cgltf_node->light)
			entity->set_name(cgltf_node->light->name);
		else
			entity->set_name(cgltf_node->name);

		float local_transform[16];
		cgltf_node_transform_local(cgltf_node, local_transform);

		cgltf_float* cgltf_rotation = cgltf_node->has_rotation ? cgltf_node->rotation : nullptr;
		quat rotation;
		if (cgltf_rotation)
			rotation = quat(cgltf_rotation[3], cgltf_rotation[0], cgltf_rotation[1], cgltf_rotation[2]);

		cgltf_float* cgltf_scale = cgltf_node->has_scale ? cgltf_node->scale : nullptr;
		fvec3 scale = cgltf_scale ? fvec3(cgltf_scale[0], cgltf_scale[1], cgltf_scale[2]) : fvec3::one;

		cgltf_float* cgltf_translation = cgltf_node->has_translation ? cgltf_node->translation : nullptr;
		fvec3 translation = cgltf_translation ? fvec3(cgltf_translation[0], cgltf_translation[1], cgltf_translation[2]) : fvec3::zero;


		entity->set_local_transform(transform::make(translation, rotation, scale));

		// Add components

		if (cgltf_node->mesh) {
			auto model = entity->add_component<scene::model>();

			for (uint32_t i = 0; i < cgltf_node->mesh->primitives_count; i++) {
				cgltf_primitive* primitive = cgltf_node->mesh->primitives + i;
				std::shared_ptr<mesh> mesh = get_mesh(primitive, path);
				std::shared_ptr<material> material = get_material(primitive, path);
				model->surfaces.push_back({ mesh, material });
			}

			model->recalculate_aabb();
		}

		if (entity->get_name() == std::string(cgltf_camera->name)) {
			auto camera = entity->add_component<scene::camera>();

			this->camera = entity;

			float vfov = cgltf_camera->data.perspective.yfov;
			camera->set_fov(vfov);
		}

		if (cgltf_sun_light && entity->get_name() == std::string(cgltf_sun_light->name)) {
			auto sun_light = entity->add_component<scene::sun_light>();

			this->sun_light = entity;

			sun_light->energy = math::fvec3(cgltf_sun_light->color[0], cgltf_sun_light->color[1], cgltf_sun_light->color[2]) * cgltf_sun_light->intensity;
		}

		if (cgltf_node->children_count > 0) {
			for (int i = 0; i < cgltf_node->children_count; i++)
				process_node(cgltf_node->children[i], cgltf_camera, cgltf_sun_light, entity.get(), path);
		}

		
		if (parent)
			entity->set_parent(parent->shared_from_this());
		else
			entities[entity->get_name()] = entity;

		std::cout << "Loaded: " << entity->get_name() << std::endl;
	}


	std::shared_ptr<mesh> renderer::get_mesh(cgltf_primitive* primitive, const std::filesystem::path& path) {
		std::shared_ptr<mesh> mesh = std::make_shared<core::mesh>();

		std::vector<float> positions;
		std::vector<float> tex_coords;
		std::vector<float> normals;
		std::vector<float> tangents;

		for (int i = 0; i < primitive->attributes_count; i++) {
			cgltf_attribute* attribute = primitive->attributes + i;
			cgltf_buffer* buffer = attribute->data->buffer_view->buffer;

			std::string buffer_uri = buffer->uri;

			bool buffer_loaded = is_buffer_loaded(buffer_uri);
			if (!buffer_loaded) {
				cgltf_options options = {};
				cgltf_load_buffer_file(&options, buffer->size, buffer_uri.c_str(),path.string().c_str(), &buffer->data);
			}

			cgltf_size count = attribute->data->count;
			cgltf_accessor* accessor = attribute->data;

			if (attribute->type == cgltf_attribute_type_position) {
				positions.resize(count * 3);
				cgltf_accessor_unpack_floats(accessor, positions.data(), count * 3);
			}

			else if (attribute->type == cgltf_attribute_type_texcoord) {
				tex_coords.resize(count * 2);
				cgltf_accessor_unpack_floats(accessor, tex_coords.data(), count * 2);
			}

			else if (attribute->type == cgltf_attribute_type_normal) {
				normals.resize(count * 3);
				cgltf_accessor_unpack_floats(accessor, normals.data(), count * 3);
			}

			else if (attribute->type == cgltf_attribute_type_tangent) {
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

		for (size_t i = 0, j = 0, k = 0; k < vertices_size; i += VEC_3_STRIDE, j += VEC_2_STRIDE, k++) {
			vertex& v = mesh->vertices[k];

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

		for (size_t i = 0, j = 0; i < trianges_size; i++, j += VEC_3_STRIDE) {
			mesh->triangles[i].x = indices[j];
			mesh->triangles[i].y = indices[j + 1];
			mesh->triangles[i].z = indices[j + 2];
		}

		mesh->recalculate_aabb();
		mesh->build_kd_tree();

		return mesh;
	}

	std::shared_ptr<material> renderer::get_material(cgltf_primitive* primitive, const std::filesystem::path& path) {
		cgltf_material* cgltf_material = primitive->material;
		auto material = std::make_shared<core::material>();

		if (!cgltf_material) return material;

		cgltf_float* cgltf_albedo_opacity = cgltf_material->pbr_metallic_roughness.base_color_factor; // float[4]
		cgltf_float cgltf_roughness = cgltf_material->pbr_metallic_roughness.roughness_factor;
		cgltf_float cgltf_metallic = cgltf_material->pbr_metallic_roughness.metallic_factor;
		cgltf_float* cgltf_emissive = cgltf_material->emissive_factor; // float[3]
		cgltf_alpha_mode cgltf_alpha_mode = cgltf_material->alpha_mode;

		auto cgltf_normal_texture = cgltf_material->normal_texture;
		auto cgltf_albedo_opacity_texture = cgltf_material->pbr_metallic_roughness.base_color_texture;
		auto cgltf_occlusion_texture = cgltf_material->occlusion_texture;
		auto cgltf_roughness_metallic_texture = cgltf_material->pbr_metallic_roughness.metallic_roughness_texture;
		auto cgltf_emissive_texture = cgltf_material->emissive_texture;

		std::string cgltf_normal_tex_path = cgltf_normal_texture.texture ? cgltf_normal_texture.texture->image->uri : "";
		std::string cgltf_albedo_opacity_tex_path = cgltf_albedo_opacity_texture.texture ? cgltf_albedo_opacity_texture.texture->image->uri : "";
		std::string cgltf_occlusion_tex_path = cgltf_occlusion_texture.texture ? cgltf_occlusion_texture.texture->image->uri : "";
		std::string cgltf_roughness_metallic_tex_path = cgltf_roughness_metallic_texture.texture ? cgltf_roughness_metallic_texture.texture->image->uri : "";
		std::string cgltf_emissive_tex_path = cgltf_emissive_texture.texture ? cgltf_emissive_texture.texture->image->uri : "";

		material->albedo_fac = fvec3(cgltf_albedo_opacity[0], cgltf_albedo_opacity[1], cgltf_albedo_opacity[2]);
		material->opacity_fac = cgltf_albedo_opacity[3];
		material->roughness_fac = cgltf_roughness;
		material->metallic_fac = cgltf_metallic;
		material->emissive_fac = fvec3(cgltf_emissive[0], cgltf_emissive[1], cgltf_emissive[2]);

		auto gltf_dir = path.parent_path();

		if (!cgltf_normal_tex_path.empty()) {
			auto normal_tex = get_cached_texture(gltf_dir / cgltf_normal_tex_path.c_str(), false);
			material->normal_tex = normal_tex;
		}

		if (!cgltf_albedo_opacity_tex_path.empty()) {
			auto albedo_opacity_tex = get_cached_texture(gltf_dir / cgltf_albedo_opacity_tex_path.c_str(), true);
			material->albedo_tex = albedo_opacity_tex;
			if (cgltf_alpha_mode != cgltf_alpha_mode_opaque)
				material->opacity_tex = albedo_opacity_tex;
		}

		if (!cgltf_occlusion_tex_path.empty()) {
			auto occlusion_tex = get_cached_texture(gltf_dir / cgltf_occlusion_tex_path.c_str(), false);
			material->occlusion_tex = occlusion_tex;
		}

		if (!cgltf_roughness_metallic_tex_path.empty()) {
			auto roughness_metallic_tex = get_cached_texture(gltf_dir / cgltf_roughness_metallic_tex_path.c_str(),
				false);
			material->roughness_tex = roughness_metallic_tex;
			material->metallic_tex = roughness_metallic_tex;
		}

		if (!cgltf_emissive_tex_path.empty()) {
			auto emissive_tex = get_cached_texture(gltf_dir / cgltf_emissive_tex_path.c_str(), true);
			material->emissive_tex = emissive_tex;
		}

		std::string name{ cgltf_material->name };
		if (name.find("shadow") != std::string::npos && name.find("catcher") != std::string::npos)
			material->shadow_catcher = true;

		return material;
	}


	std::vector<uint8_t> renderer::render() const {
		util::thread_pool pool(thread_count);

		std::vector<std::shared_ptr<util::future>> todo;
		todo.reserve(resolution.y);

		// Claiming pixels by opaque samples is needed for proper transparent background blending
		struct pixel {
			math::fvec3 color;
			float alpha;
			bool claimed;
		};

		std::vector<std::vector<pixel>> pixels;
		pixels.resize(resolution.x);
		for (auto& column : pixels)
			column.resize(resolution.y, {fvec3::zero, 0, false});

		auto img = std::make_shared<image::image>(resolution, 4, false, true);

		for (uint32_t sample = 0; sample < sample_count; sample++) {
			std::cout << "Drawing sample " << sample + 1 << " out of " << sample_count << '.' << std::endl;

			for (uint32_t y = 0; y < resolution.y; y++) {
				todo.push_back(pool.submit([&, y](uint32_t) {
					for (uint32_t x = 0; x < resolution.x; x++) {
						uvec2 pixel(x, y);

						// Do not offset the first sample so we can get a consistent alpha mask for smart blending
						fvec2 aa_offset = fvec2(rand(), rand());

						fvec2 ndc = ((fvec2(pixel) + aa_offset) /
							resolution) * 2 - fvec2::one;
						ndc.y = -ndc.y;
						float ratio = static_cast<float>(resolution.x) / resolution.y;

						ray ray = camera->get_component<scene::camera>()->get_ray(ndc, ratio);
						fvec4 data = trace(bounce_count, ray);

						// Smart blending - needed for transparent background
						if (transparent_background) {
							if (data.w > 0.5 && !pixels[x][y].claimed) {
								// If an opaque sample will claim this pixel
								pixels[x][y].color = fvec3(data); // Overwrite the color
								pixels[x][y].alpha = 1 / (sample + 1); // And blend the alpha
								pixels[x][y].claimed = true; // Mark the pixel as claimed
								continue;
							}
							else if (data.w < 0.5 && pixels[x][y].claimed) {
								// If a transparent sample encounters a claimed pixel
								pixels[x][y].alpha = pixels[x][y].alpha * sample + data.w; // Blend only alpha
								pixels[x][y].alpha /= sample + 1;
								continue;
							}
							else if (data.w < 0.5) {
								// If transparent sample blends with an unclaimed pixel
								// Do nothing and preserve the default transparent black color
								continue;
							}
						}

						// Otherwise if an opaque sample blends with a claimed pixel (or transparent background is disabled)
						pixels[x][y].color = pixels[x][y].color * sample + fvec3(data); // Blend color
						pixels[x][y].color /= sample + 1;
						pixels[x][y].alpha = pixels[x][y].alpha * sample + data.w; // Blend alpha
						pixels[x][y].alpha /= sample + 1;
					}
				}));
			}

			for (auto& future : todo)
				future->wait();

			todo.clear();

			if (sample != 0 && sample % 5 == 0 || sample == sample_count - 1) {
				std::cout << "Saving..." << std::endl;

				for (uint32_t y = 0; y < resolution.y; y++) {
					for (uint32_t x = 0; x < resolution.x; x++) {
						fvec3 color = tonemap_approx_aces(pixels[x][y].color);
						float alpha = pixels[x][y].alpha;

						uvec2 pixel(x, y);
						img->write(pixel, 0, color.x);
						img->write(pixel, 1, color.y);
						img->write(pixel, 2, color.z);
						img->write(pixel, 3, alpha);
					}
				}
			}
		}

		return img->save_to_memory_png();
	}

	fvec3 renderer::intersect_result::get_normal() const {
		vec3 binormal = cross(normal, tangent);
		fmat3 tbn(tangent, binormal, normal);

		return tbn * material->get_normal(tex_coord);
	}

	fvec4 renderer::trace(uint8_t bounce, const ray& ray) const {
		if (bounce == 0)
			return fvec4::future;

		auto result = intersect(ray);

		if (!result.hit) {
			float alpha = transparent_background ? 0 : 1;

			if (environment)
				return fvec4(fvec3(environment->sample(equirectangular_proj(ray.get_dir()))) * environment_factor,
				             alpha);
			else
				return fvec4(environment_factor, alpha);
		}

		if (visualize_kd_tree_depth)
			return fvec4(result.position, 1);

		// Material properties

		fvec3 albedo = result.material->get_albedo(result.tex_coord);
		float opacity = result.material->get_opacity(result.tex_coord);
		float roughness = result.material->get_roughness(result.tex_coord);
		float metallic = result.material->get_metallic(result.tex_coord);
		fvec3 emissive = result.material->get_emissive(result.tex_coord) * 10; // DEBUG
		float ior = result.material->ior;

		// Handle opacity
		if (!math::is_approx(opacity, 1) && rand() > opacity) {
			geometry::ray opacity_ray(
				result.position + ray.get_dir() * math::epsilon,
				ray.get_dir()
			);
			return trace(bounce, opacity_ray);
		}

		fvec3 normal = result.get_normal();
		fvec3 outcoming = -ray.get_dir();

		// With smooth shading the outcoming vector may point under the surface
		if (math::dot(normal, outcoming) <= 0)
			return fvec4::future;
		// outcoming = reflect(outcoming, normal); // Do not use this
		// We cannot correct the outcoming vector because that will result in rays getting trapped in a surface
		// A new ray might intersect at its starting point and get reflected back to it multiple times

		// Correct the roughness

		roughness = math::max(roughness, 0.05F); // Small roughness might cause precision artifacts

		// Are we going to sample specular or diffuse BRDF lobe?

		float specular_probability = pbr::fresnel(outcoming, reflect(-outcoming, normal), ior);
		specular_probability = math::max(specular_probability, metallic);
		bool specular_sample = core::rand() < specular_probability;

		// Direct Lighting

		fvec3 direct_out;

		if (sun_light) {
			fvec3 direct_incoming = sun_light->get_global_transform().basis * fvec3::backward;
			direct_incoming = util::rand_cone_vec(rand(), math::cos(rand() * sun_light->get_component<scene::sun_light>()->angular_radius),
			                                      direct_incoming);

			// Sunlight lobe might intersect with the surface, so let's avoid that
			if (math::dot(normal, direct_incoming) > 0) {
				geometry::ray direct_ray(
					result.position + direct_incoming * math::epsilon,
					direct_incoming
				);
				auto direct_result = intersect(direct_ray);

				if (!direct_result.hit) {
					// If a shadow catcher is not in shadow, treat it as if it was fully transparent
					if (result.material->shadow_catcher && bounce == bounce_count) {
						geometry::ray opacity_ray(
							result.position + ray.get_dir() * math::epsilon,
							ray.get_dir()
						);
						return trace(bounce, opacity_ray);
					}

					// Diffuse BRDF

					float diffuse_pdf = pbr::pdf_diffuse(normal, direct_incoming);
					fvec3 diffuse_brdf = diffuse_pdf * albedo;

					// Specular BRDF

					float specular_pdf = pbr::pdf_specular(normal, outcoming, direct_incoming, roughness);
					fvec3 specular_brdf(specular_pdf);

					// Fresnel

					fvec3 fresnel = lerp(fvec3(0.04F), albedo, metallic);
					{
						fvec3 halfway = normalize(outcoming + direct_incoming);
						float cos_theta = dot(outcoming, halfway);

						fresnel = lerp(fresnel, fvec3::one, math::pow(1 - cos_theta, 5));
					}

					// Final BRDF

					diffuse_brdf = lerp(diffuse_brdf, fvec3::zero, metallic);
					// Metallic should realistically be either 1 or 0
					fvec3 brdf = lerp(diffuse_brdf, specular_brdf, fresnel);

					// Final PDF

					// 100% chance of hitting the sun
					diffuse_pdf = 1;
					specular_pdf = 1;
					float pdf = lerp(diffuse_pdf, specular_pdf, specular_probability);

					fvec3 direct_in = sun_light->get_component<scene::sun_light>()->energy;
					direct_out = brdf * direct_in / math::max(pdf, math::epsilon);
					direct_out = math::clamp(direct_out, fvec3::zero, direct_in);
				}
				else {
					// If a shadow catcher is in shadow, return zero
					if (result.material->shadow_catcher && bounce == bounce_count)
						return fvec4::future;
				}
			}
		}

		// Indirect Lighting

		fvec3 indirect_out;

		// Importance sampling

		fvec2 rand(core::rand(), core::rand());
		fvec3 indirect_incoming = specular_sample
			                          ? pbr::importance_specular(rand, normal, outcoming, roughness)
			                          : pbr::importance_diffuse(rand, normal, outcoming);

		// Specular BRDF lobe might intersect with the surface, so let's avoid that
		if (math::dot(normal, indirect_incoming) > 0) {
			// Diffuse BRDF

			float diffuse_pdf = pbr::pdf_diffuse(normal, indirect_incoming);
			fvec3 diffuse_brdf = diffuse_pdf * albedo;

			// Specular BRDF

			float specular_pdf = pbr::pdf_specular(normal, outcoming, indirect_incoming, roughness);
			fvec3 specular_brdf(specular_pdf);

			// Fresnel

			fvec3 fresnel = lerp(fvec3(0.04F), albedo, metallic);
			{
				fvec3 halfway = normalize(outcoming + indirect_incoming);
				float cos_theta = dot(outcoming, halfway);

				fresnel = lerp(fresnel, fvec3::one, math::pow(1 - cos_theta, 5));
			}

			// Final BRDF

			diffuse_brdf = lerp(diffuse_brdf, fvec3::zero, metallic); // Metallic should realistically be either 1 or 0
			fvec3 brdf = lerp(diffuse_brdf, specular_brdf, fresnel);

			// Final PDF

			float pdf = lerp(diffuse_pdf, specular_pdf, specular_probability);

			// Next bounce

			geometry::ray indirect_ray(
				result.position + indirect_incoming * math::epsilon,
				indirect_incoming
			);

			// This division by PDF partially cancels out with the BRDF
			fvec3 indirect_in = fvec3(trace(bounce - 1, indirect_ray));
			indirect_out = brdf * indirect_in / math::max(pdf, math::epsilon);

			// We refuse to return more than what was given and this prevents hot pixels
			indirect_out = math::clamp(indirect_out, fvec3::zero, indirect_in);

			// if (pdf == 0) {
			if (std::isnan(math::length(indirect_out))) {
				// if (math::any(indirect_out > fvec3(1000))) {
				float n_dot_o = dot(normal, outcoming);
				float n_dot_i = dot(normal, indirect_incoming);

				std::cout << "NaN value at outcoming: " << outcoming << " indirect_incoming: " << indirect_incoming <<
					" brdf: " << brdf << std::endl;
				std::cout << "fresnel: " << fresnel << " diffuse_brdf: " << diffuse_brdf << " specular_brdf: " <<
					specular_brdf << std::endl;
				std::cout << "n_dot_o: " << n_dot_o << " n_dot_i: " << n_dot_i << " bounce: " << static_cast<uint32_t>(
					bounce) << std::endl;
				std::cout << "position: " << result.position << " pdf: " << pdf << " indirect_in: " << indirect_in <<
					" indirect_out: " << indirect_out << std::endl;
				std::cout << "diffuse_pdf: " << diffuse_pdf << " specular_pdf: " << specular_pdf << " roughness: " <<
					roughness << std::endl;
				std::cout << std::endl;
			}
		}

		return fvec4(direct_out + indirect_out + emissive, 1);
	}

	renderer::intersect_result renderer::intersect(const ray& ray) const {
		std::stack<entity*> stack;
		for (const auto& [_, entity] : entities) {
			stack.push(entity.get());
		}

		model::intersection nearest_hit;

		while (!stack.empty()) {
			entity* entity = stack.top();
			stack.pop();

			for (const auto& child : entity->get_children())
				stack.push(child.get());

			if (auto model = entity->get_component<scene::model>()) {
				auto hit = model->intersect(ray, visualize_kd_tree_depth);

				if (!hit.has_hit())
					continue;

				if (hit.distance < nearest_hit.distance
					|| !nearest_hit.has_hit()) {
					nearest_hit = hit;
				}
			}
		}


		if (!nearest_hit.has_hit())
			return {false};

		if (visualize_kd_tree_depth) {
			return {
				true, // Yes, we've hit
				nullptr, // No material
				nearest_hit.barycentric, // Random voxel color instead of position
				fvec2::zero, // No tex coord
				fvec3::zero, // No normal
				fvec3::zero // No tangent
			};
		}

		const auto& mesh = nearest_hit.surface->mesh;
		const auto& material = nearest_hit.surface->material;

		uvec3& indices = mesh->triangles[nearest_hit.triangle_index];
		auto& v1 = mesh->vertices[indices.x];
		auto& v2 = mesh->vertices[indices.y];
		auto& v3 = mesh->vertices[indices.z];

		// Normals will have to be normalized if transform applies scale
		transform transform = nearest_hit.transform;
		fmat3 normal_matrix = transpose(inverse(nearest_hit.transform.basis));

		fvec3 position = transform * (
			v1.position * nearest_hit.barycentric.x +
			v2.position * nearest_hit.barycentric.y +
			v3.position * nearest_hit.barycentric.z);
		fvec2 tex_coord =
			v1.tex_coord * nearest_hit.barycentric.x +
			v2.tex_coord * nearest_hit.barycentric.y +
			v3.tex_coord * nearest_hit.barycentric.z;
		fvec3 normal = normalize(normal_matrix * (
			v1.normal * nearest_hit.barycentric.x +
			v2.normal * nearest_hit.barycentric.y +
			v3.normal * nearest_hit.barycentric.z));
		fvec3 tangent = normalize(normal_matrix * (
			v1.tangent * nearest_hit.barycentric.x +
			v2.tangent * nearest_hit.barycentric.y +
			v3.tangent * nearest_hit.barycentric.z));

		return {
			true,
			material,
			position,
			tex_coord,
			normal,
			tangent
		};
	}
}
