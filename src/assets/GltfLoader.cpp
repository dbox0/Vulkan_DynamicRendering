#include "GltfLoader.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <tuple>
#include <unordered_map>

#include <volk.h>
#include <glm/gtc/type_ptr.hpp>

#include "Mesh.h"
#include "TextureCache.h"
#include "../../third_party/tiny_gltf_v3.h"
#include "../../third_party/stb_image.h"

#include "../common/errors.h"
#include "../render/resources/GeometryStore.h"
#include "../render/resources/ResourceStore.h"
#include "../render/core/VulkanContext.h"
#include "../scene/Scene.h"
#include "../render/GpuShared.h"
struct Image
{
    int width  = 0;
    int height = 0;
    int channels = 0;
    unsigned char *data = nullptr;

    // glTF images carry no colour space of their own -- only the material
    // slot that samples one knows whether it is colour or data. Decided by
    // the pre-pass in load(), consumed by uploadImages().
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
};


bool GltfLoader::load(const std::filesystem::path &filepath)
{
    return loadImpl(filepath, true, nullptr);
}

bool GltfLoader::loadMeshesOnly(const std::filesystem::path &filepath, std::vector<uint32_t> &meshIdsOut)
{
    meshIdsOut.clear();
    return loadImpl(filepath, false, &meshIdsOut);
}

bool GltfLoader::loadImpl(const std::filesystem::path &filepath, bool importNodes,
                          std::vector<uint32_t> *meshIdsOut)
{
    // Recorded into every Mesh this file produces so the scene serializer
    // can write "load this file, take mesh N" as the asset reference.
    // Use the full path here; TextureCache::toRelative() strips ASSET_DIR.
    m_sourcePath = filepath.lexically_normal().string();

    if (!std::filesystem::exists(filepath)) {
        std::cerr << "[error] File does not exist: " << filepath << std::endl;
        return false;
    }

    std::cout << "Loading GLTF: " << filepath << std::endl;

    tg3_model model;
    tg3_parse_options opts;
    tg3_error_stack errors;

    tg3_parse_options_init(&opts);
    tg3_error_stack_init(&errors);

    const std::string pathStr = filepath.string();
    const tg3_error_code parseResult =
        tg3_parse_file(&model, &errors, pathStr.c_str(), pathStr.size(), &opts);

    if (parseResult != TG3_OK) {
        std::cerr << "[error] Error parsing GLTF file:" << std::endl;
        for (int i = 0; i < errors.count; ++i) {
            std::cerr << "  " << errors.entries[i].message << std::endl;
        }
        tg3_error_stack_free(&errors);
        return false;
    }
    tg3_error_stack_free(&errors);

    const std::filesystem::path imageDir = filepath.parent_path();

    std::vector<Image> images = loadImages(model, imageDir);   // -> RAM
    assignImageColorSpaces(model, images);                     // sRGB vs linear
    std::vector<uint32_t> imageIds = uploadImages(images);     // -> VRAM

    // Names are for the editor's texture previews, so they can only be set
    // after upload -- image IDs do not exist until then.
    for (size_t i = 0; i < imageIds.size(); ++i) {
        // Images that failed to load all share the one error image; naming
        // that after a file would mislabel it everywhere else it appears.
        if (!images[i].data || imageIds[i] == m_resources.errorImageId()) {
            continue;
        }
        const tg3_str &uri = model.images[i].uri;
        if (!uri.data) {
            continue;
        }
        const std::string uriStr(uri.data, uri.len);
        m_resources.setImageName(imageIds[i], uriStr);

        // Provenance, so a material using this image can be serialized later.
        // Empty for anything outside ASSET_DIR -- a model dragged in from
        // elsewhere on disk stays unsaveable until it is copied into the
        // project, which is the honest answer rather than writing an absolute
        // path into a .mat.
        const std::string relative = m_cache.toRelative(imageDir / uriStr);
        if (!relative.empty()) {
            m_cache.registerImage(imageIds[i], relative,
                                  images[i].format == VK_FORMAT_R8G8B8A8_SRGB);
        }
    }

    for (const Image &image : images) {
        stbi_image_free(image.data);
    }

    const std::vector<uint32_t> samplerIds  = loadSamplers(model);
    const std::vector<uint32_t> textureIds  = loadTextures(model, imageIds, samplerIds);
    const std::vector<uint32_t> materialIds = loadMaterials(model, textureIds);
    const std::vector<uint32_t> meshIds     = loadMeshes(model, materialIds);
    if (meshIdsOut) {
        *meshIdsOut = meshIds;
    }

    // Scene nodes.
    if (importNodes && model.scenes_count > 0) {
        const int sceneIndex = model.default_scene != -1 ? model.default_scene : 0;
        const tg3_scene *scene = &model.scenes[sceneIndex];

        for (int i = 0; i < scene->nodes_count; ++i) {
            // The root chain is bookkept by Scene now, so the loader no longer
            // tracks m_rootNodeId / m_lastRootNodeId itself.
            const uint32_t nodeId = importNode(model, scene->nodes[i], 0, 0, meshIds);
            m_scene.addRootNode(nodeId);
        }
    }

    tg3_model_free(&model);
    std::cout << "GLTF loaded successfully" << std::endl;
    return true;
}

// ============================================================================
// nodes
// ============================================================================

uint32_t GltfLoader::importNode(const tg3_model &model, int32_t nodeIndex,
                                uint32_t parentId, uint32_t prevSiblingId,
                                const std::vector<uint32_t> &meshIds)
{
    const tg3_node &tg3Node = model.nodes[nodeIndex];

    NodeWorld &nodeWorld = m_scene.nodes();
    const uint32_t nodeId = nodeWorld.createNode().second;

    // Deliberately NOT holding the Node& across the recursive child loop
    // below: createNode() can reallocate the underlying vector once the scene
    // exceeds maxNodes, which silently dangles the reference. Re-fetch by ID
    // around each recursion instead.
    {
        Node &node = nodeWorld.getNode(nodeId);
        node.parentId = parentId;

        if (tg3Node.has_matrix) {
            glm::mat4 transform(1.0f);
            float *transPtr = glm::value_ptr(transform);
            for (int i = 0; i < 16; ++i) {
                transPtr[i] = static_cast<float>(tg3Node.matrix[i]);
            }
            node.setTransform(transform);
        } else {
            const glm::vec3 translation(tg3Node.translation[0], tg3Node.translation[1], tg3Node.translation[2]);
            // glTF stores quaternions XYZW; glm's constructor takes WXYZ.
            const glm::quat rotation(static_cast<float>(tg3Node.rotation[3]),
                                     static_cast<float>(tg3Node.rotation[0]),
                                     static_cast<float>(tg3Node.rotation[1]),
                                     static_cast<float>(tg3Node.rotation[2]));
            const glm::vec3 scale(tg3Node.scale[0], tg3Node.scale[1], tg3Node.scale[2]);

            node.setTranslation(translation);
            node.setRotation(rotation);
            node.setScale(scale);
        }

        if (tg3Node.mesh != -1 && static_cast<size_t>(tg3Node.mesh) < meshIds.size()) {
            node.meshId = meshIds[tg3Node.mesh];
        }
    }

    if (prevSiblingId) {
        nodeWorld.getNode(prevSiblingId).nextSiblingId = nodeId;
    }

    uint32_t lastChildId  = 0;
    uint32_t firstChildId = 0;
    for (int i = 0; i < tg3Node.children_count; ++i) {
        lastChildId = importNode(model, tg3Node.children[i], nodeId, lastChildId, meshIds);
        if (!firstChildId) {
            firstChildId = lastChildId;
        }
    }
    if (firstChildId) {
        nodeWorld.getNode(nodeId).firstChildId = firstChildId;
    }

    return nodeId;
}

// ============================================================================
// meshes
// ============================================================================

std::vector<uint32_t> GltfLoader::loadMeshes(const tg3_model &model,
                                             const std::vector<uint32_t> &materialIds)
{
    std::vector<uint32_t> meshIds(model.meshes_count);

    for (int i = 0; i < model.meshes_count; ++i) {
        Mesh mesh;
        const tg3_mesh *tg3mesh = &model.meshes[i];
        mesh.name = tg3mesh->name.data != nullptr ? tg3mesh->name.data : "Unnamed mesh";

        mesh.subMeshes.resize(tg3mesh->primitives_count);

        for (int u = 0; u < tg3mesh->primitives_count; ++u) {
            const tg3_primitive *primitive = &tg3mesh->primitives[u];
            SubMesh &subMesh = mesh.subMeshes[u];

            // primitive->material is -1 for the glTF default material.
            subMesh.materialId =
                (primitive->material != -1 && static_cast<size_t>(primitive->material) < materialIds.size())
                    ? materialIds[primitive->material]
                    : 0;

            // Find POSITION first: its count determines how many vertex slots
            // this submesh needs, and every other attribute writes into them.
            const tg3_accessor *positionAccessor = nullptr;
            for (int v = 0; v < primitive->attributes_count; ++v) {
                const tg3_str_int_pair *attr = &primitive->attributes[v];
                if (std::strcmp(attr->key.data, "POSITION") == 0) {
                    positionAccessor = &model.accessors[attr->value];
                    break;
                }
            }
            if (!positionAccessor) {
                showError("glTF primitive has no POSITION attribute; skipping");
                continue;
            }

            assert(positionAccessor->type == TG3_TYPE_VEC3 &&
                   positionAccessor->component_type == TG3_COMPONENT_TYPE_FLOAT);

            subMesh.vertexCount = positionAccessor->count;
            subMesh.vertexStart = m_geometry.allocateVertices(positionAccessor->count);

            if (subMesh.vertexStart == GeometryStore::kInvalidOffset) {
                std::cerr << "[warn] Vertex budget exhausted in mesh '" << mesh.name
              << "'; primitive " << u << " skipped" << std::endl;
                subMesh = SubMesh{};
                continue;
            }
            subMesh.vertexCount = positionAccessor->count;

            // Copies one float attribute into the vertex slots starting at
            // vertexStart. Captures vertexStart rather than reading a member,
            // which is what makes this independent of GeometryStore's cursor.
            const size_t vertexStart = subMesh.vertexStart;


            // glTF allows normalized u8/u16 for COLOR_0 and TEXCOORD_0
            auto readComponent = [](const unsigned char *base, const tg3_accessor *accessor, int c) -> float
            {
                switch (accessor->component_type) {
                    case TG3_COMPONENT_TYPE_FLOAT:
                        return reinterpret_cast<const float *>(base)[c];
                    case TG3_COMPONENT_TYPE_UNSIGNED_BYTE:
                        return static_cast<float>(base[c]) / 255.0f;
                    case TG3_COMPONENT_TYPE_UNSIGNED_SHORT:
                        return static_cast<float>(reinterpret_cast<const uint16_t *>(base)[c]) / 65535.0f;
                    default:
                        return 0.0f;
                }
            };

            // Mirrors what readComponent decodes. glTF requires the integer
            // forms be normalized; an unnormalized one would read as 0..255

            auto isReadableComponent = [](const tg3_accessor *accessor) -> bool
            {
                switch (accessor->component_type) {
                    case TG3_COMPONENT_TYPE_FLOAT:
                        return true;
                    case TG3_COMPONENT_TYPE_UNSIGNED_BYTE:
                    case TG3_COMPONENT_TYPE_UNSIGNED_SHORT:
                        return accessor->normalized != 0;
                    default:
                        return false;
                }
            };

            auto writeAttribute = [this, &model, vertexStart, &readComponent]<typename T>(
                T Vertex::*member, const tg3_str_int_pair *attr)
            {
                const tg3_accessor    *accessor    = &model.accessors[attr->value];
                const tg3_buffer_view *buffer_view = &model.buffer_views[accessor->buffer_view];
                const tg3_buffer      *buffer      = &model.buffers[buffer_view->buffer];

                const size_t bufferOffset = buffer_view->byte_offset + accessor->byte_offset;

                // Stride comes from the ACCESSOR, not from the destination
                // member: a tightly packed VEC4 has stride 16 while
                // sizeof(glm::vec3) is 12, so the old sizeof(T) fallback read
                // every vertex after the first at the wrong offset.
                const size_t stride = static_cast<size_t>(tg3_accessor_byte_stride(accessor, buffer_view));

                // How many floats the file actually supplies -- a VEC3
                // COLOR_0 feeding a vec4 member must not read a 4th float.
                const int components = tg3_num_components(accessor->type);

                for (uint64_t index = 0; index < accessor->count; ++index) {
                    const unsigned char *element = buffer->data.data + bufferOffset + index * stride;

                    Vertex *vertex = m_geometry.vertexAt(vertexStart + index);
                    if constexpr (std::is_same_v<T, glm::vec4>) {
                        vertex->*member = glm::vec4(readComponent(element, accessor, 0),
                                                    readComponent(element, accessor, 1),
                                                    readComponent(element, accessor, 2),
                                                    components >= 4 ? readComponent(element, accessor, 3) : 1.0f);
                    } else if constexpr (std::is_same_v<T, glm::vec3>) {
                        vertex->*member = glm::vec3(readComponent(element, accessor, 0),
                                                    readComponent(element, accessor, 1),
                                                    readComponent(element, accessor, 2));
                    } else if constexpr (std::is_same_v<T, glm::vec2>) {
                        vertex->*member = glm::vec2(readComponent(element, accessor, 0),
                                                    readComponent(element, accessor, 1));
                    } else {
                        static_assert(sizeof(T) == 0, "writeAttribute: unhandled attribute type");
                    }
                }
            };

            for (int v = 0; v < primitive->attributes_count; ++v) {
                const tg3_str_int_pair *attr = &primitive->attributes[v];
                const tg3_accessor *accessor = &model.accessors[attr->value];

                if (std::strcmp(attr->key.data, "POSITION") == 0) {
                    writeAttribute(&Vertex::position, attr);
                } else if (std::strcmp(attr->key.data, "NORMAL") == 0) {
                    assert(accessor->type == TG3_TYPE_VEC3 && accessor->component_type == TG3_COMPONENT_TYPE_FLOAT);
                    writeAttribute(&Vertex::normal, attr);
                } else if (std::strcmp(attr->key.data, "COLOR_0") == 0) {
                    assert(accessor->type == TG3_TYPE_VEC3 || accessor->type == TG3_TYPE_VEC4);
                    assert(isReadableComponent(accessor));
                    if (isReadableComponent(accessor)) {
                        writeAttribute(&Vertex::color, attr);
                    } else {
                        std::cerr << "[warn] COLOR_0 has an unsupported component type; "
                                     "leaving vertex colours at white" << std::endl;
                    }
                } else if (std::strcmp(attr->key.data, "TANGENT") == 0) {
                    // w carries the bitangent sign. No TANGENT leaves w at 0,
                    // which the fragment shader reads as "derive a tangent
                    // frame from screen-space derivatives instead".
                    assert(accessor->type == TG3_TYPE_VEC4 && accessor->component_type == TG3_COMPONENT_TYPE_FLOAT);
                    writeAttribute(&Vertex::tangent, attr);
                } else if (std::strcmp(attr->key.data, "TEXCOORD_0") == 0) {
                    assert(accessor->type == TG3_TYPE_VEC2);
                    assert(isReadableComponent(accessor));
                    if (isReadableComponent(accessor)) {
                        writeAttribute(&Vertex::uv, attr);
                    } else {
                        std::cerr << "[warn] TEXCOORD_0 has an unsupported component type; "
                                     "UVs will be zero" << std::endl;
                    }
                }
            }

            // Indices.
            if (primitive->indices != -1) {
                const tg3_accessor    *accessor    = &model.accessors[primitive->indices];
                const tg3_buffer_view *buffer_view = &model.buffer_views[accessor->buffer_view];
                const tg3_buffer      *buffer      = &model.buffers[buffer_view->buffer];

                subMesh.indexCount = accessor->count;
                const size_t indexStart = m_geometry.allocateIndices(accessor->count);

                // Was testing subMesh.indexStart, which is still 0 here -- an
                // exhausted index budget sailed straight through and wrote at
                // offset 0.
                if (indexStart == GeometryStore::kInvalidOffset) {
                    std::cerr << "Index budget exhausted"<< std::endl;
                    subMesh = SubMesh{};
                    continue;
                }

                subMesh.indexStart = indexStart;
                subMesh.indexCount = accessor->count;

                const unsigned char *src = buffer->data.data + buffer_view->byte_offset + accessor->byte_offset;
                uint32_t *dst = m_geometry.indexAt(subMesh.indexStart);

                if (accessor->component_type == TG3_COMPONENT_TYPE_UNSIGNED_INT) {
                    std::memcpy(dst, src, accessor->count * sizeof(uint32_t));
                } else if (accessor->component_type == TG3_COMPONENT_TYPE_UNSIGNED_SHORT) {
                    const uint16_t *src16 = reinterpret_cast<const uint16_t *>(src);
                    for (uint64_t idx = 0; idx < accessor->count; ++idx) {
                        dst[idx] = static_cast<uint32_t>(src16[idx]);
                    }
                } else if (accessor->component_type == TG3_COMPONENT_TYPE_UNSIGNED_BYTE) {
                    for (uint64_t idx = 0; idx < accessor->count; ++idx) {
                        dst[idx] = static_cast<uint32_t>(src[idx]);
                    }
                } else {
                    showError("Unsupported glTF index component type");
                }
            }
        }

        mesh.sourcePath      = m_sourcePath;
        mesh.sourceMeshIndex = static_cast<int32_t>(i);
        meshIds[i] = m_geometry.addMesh(std::move(mesh));
    }
    return meshIds;
}

// ============================================================================
// materials / textures / samplers / images
// ============================================================================

// Walks the MATERIALS to decide each image's format, because that is the only
// place the information exists: the same PNG is sRGB as a base colour map and
// linear as a roughness map. Sampling a normal map through an sRGB view bends
// the normals in a way that looks almost right, which is the worst kind of bug.
void GltfLoader::assignImageColorSpaces(const tg3_model &model, std::vector<Image> &images) const
{
    // Anything no material references keeps the linear default; it is never
    // sampled anyway.
    auto markSrgb = [&](int32_t textureIndex) {
        if (textureIndex < 0 || static_cast<uint32_t>(textureIndex) >= model.textures_count) {
            return;
        }
        const int32_t source = model.textures[textureIndex].source;
        if (source >= 0 && static_cast<size_t>(source) < images.size()) {
            images[source].format = VK_FORMAT_R8G8B8A8_SRGB;
        }
    };

    for (uint32_t i = 0; i < model.materials_count; ++i) {
        // Only base colour and emissive are colour. Metallic-roughness,
        // normal and occlusion are all data.
        markSrgb(model.materials[i].pbr_metallic_roughness.base_color_texture.index);
        markSrgb(model.materials[i].emissive_texture.index);
    }
}

std::vector<uint32_t> GltfLoader::loadMaterials(const tg3_model &model,
                                                const std::vector<uint32_t> &textureIds)
{
    std::vector<uint32_t> materialIds(model.materials_count);

    // 0 means "this material has no such texture" -- ResourceStore::toGpu
    // substitutes the white default. Out-of-range indices are malformed
    // files, so they get 0 too rather than the error texture.
    auto texId = [&](int32_t index) -> uint32_t {
        return (index >= 0 && static_cast<size_t>(index) < textureIds.size())
                   ? textureIds[index]
                   : 0;
    };

    for (uint32_t i = 0; i < model.materials_count; ++i) {
        const tg3_material &src = model.materials[i];
        const auto &pbr = src.pbr_metallic_roughness;

        Material material;
        material.name = src.name.data ? std::string(src.name.data, src.name.len)
                                      : std::string();

        material.baseColorFactor = glm::vec4(pbr.base_color_factor[0],
                                             pbr.base_color_factor[1],
                                             pbr.base_color_factor[2],
                                             pbr.base_color_factor[3]);
        material.metallicFactor  = static_cast<float>(pbr.metallic_factor);
        material.roughnessFactor = static_cast<float>(pbr.roughness_factor);

        material.emissiveFactor = glm::vec3(src.emissive_factor[0],
                                            src.emissive_factor[1],
                                            src.emissive_factor[2]);

        material.normalScale       = static_cast<float>(src.normal_texture.scale);
        material.occlusionStrength = static_cast<float>(src.occlusion_texture.strength);
        material.alphaCutoff       = static_cast<float>(src.alpha_cutoff);
        material.doubleSided       = src.double_sided != 0;

        material.alphaMode = tg3_str_equals_cstr(src.alpha_mode, "MASK")  ? AlphaMode::Mask
                           : tg3_str_equals_cstr(src.alpha_mode, "BLEND") ? AlphaMode::Blend
                                                                          : AlphaMode::Opaque;

        material.baseColorTexture         = texId(pbr.base_color_texture.index);
        material.metallicRoughnessTexture = texId(pbr.metallic_roughness_texture.index);
        material.normalTexture            = texId(src.normal_texture.index);
        material.occlusionTexture         = texId(src.occlusion_texture.index);
        material.emissiveTexture          = texId(src.emissive_texture.index);

        materialIds[i] = m_resources.addMaterial(material, AssetOrigin::Imported);
    }
    return materialIds;
}

std::vector<uint32_t> GltfLoader::loadTextures(const tg3_model &model,
                                               const std::vector<uint32_t> &imageIds,
                                               const std::vector<uint32_t> &samplerIds)
{
    std::vector<uint32_t> textureIds(model.textures_count);

    for (int i = 0; i < model.textures_count; ++i) {
        const tg3_texture &tex = model.textures[i];

        // Both source and sampler are optional in glTF and come back as -1.
        // Indexing the vectors with those was reading out of bounds.
        const uint32_t imageId =
            (tex.source != -1 && static_cast<size_t>(tex.source) < imageIds.size())
                ? imageIds[tex.source]
                : m_resources.errorImageId();

        const uint32_t samplerId =
            (tex.sampler != -1 && static_cast<size_t>(tex.sampler) < samplerIds.size())
                ? samplerIds[tex.sampler]
                // A texture with no sampler is legal glTF (use the defaults),
                // so this falls back to the default SAMPLER. An image ID is a
                // different namespace and would land on an arbitrary sampler.
                : m_resources.defaultSamplerId();

        textureIds[i] = m_resources.addTexture(imageId, samplerId);
    }
    return textureIds;
}

std::vector<uint32_t> GltfLoader::loadSamplers(const tg3_model &model)
{
    // filter -> { magFilter/minFilter, mipmapMode, maxLod }
    static const std::unordered_map<int32_t, std::tuple<VkFilter, VkSamplerMipmapMode, float>> filterMap
    {
        { TG3_TEXTURE_FILTER_NEAREST,                { VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST, 0.25f } },
        { TG3_TEXTURE_FILTER_LINEAR,                 { VK_FILTER_LINEAR,  VK_SAMPLER_MIPMAP_MODE_NEAREST, 0.25f } },
        { TG3_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR,   { VK_FILTER_LINEAR,  VK_SAMPLER_MIPMAP_MODE_LINEAR,  VK_LOD_CLAMP_NONE } },
        { TG3_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST, { VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_LOD_CLAMP_NONE } },
        { TG3_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR,  { VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_LINEAR,  VK_LOD_CLAMP_NONE } },
        { TG3_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST,  { VK_FILTER_LINEAR,  VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_LOD_CLAMP_NONE } }
    };

    static const std::unordered_map<int32_t, VkSamplerAddressMode> wrapMap
    {
        { TG3_TEXTURE_WRAP_REPEAT,          VK_SAMPLER_ADDRESS_MODE_REPEAT },
        { TG3_TEXTURE_WRAP_CLAMP_TO_EDGE,   VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE },
        { TG3_TEXTURE_WRAP_MIRRORED_REPEAT, VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT }
    };

    // .at() throws on an unknown enum value; look up defensively instead.
    auto filterOr = [](int32_t key, auto fallback, auto extractor) {
        const auto it = filterMap.find(key);
        return it == filterMap.end() ? fallback : extractor(it->second);
    };
    auto wrapOr = [](int32_t key) {
        const auto it = wrapMap.find(key);
        return it == wrapMap.end() ? VK_SAMPLER_ADDRESS_MODE_REPEAT : it->second;
    };

    std::vector<uint32_t> samplerIds(model.samplers_count);

    for (int i = 0; i < model.samplers_count; ++i) {
        const tg3_sampler &tg3Sampler = model.samplers[i];

        VkSamplerCreateInfo samplerInfo
        {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter  = filterOr(tg3Sampler.mag_filter, VK_FILTER_LINEAR,
                                   [](const auto &t) { return std::get<0>(t); }),
            .minFilter  = filterOr(tg3Sampler.min_filter, VK_FILTER_LINEAR,
                                   [](const auto &t) { return std::get<0>(t); }),
            .mipmapMode = filterOr(tg3Sampler.min_filter, VK_SAMPLER_MIPMAP_MODE_LINEAR,
                                   [](const auto &t) { return std::get<1>(t); }),
            .addressModeU = wrapOr(tg3Sampler.wrap_s),
            .addressModeV = wrapOr(tg3Sampler.wrap_t),
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,   // change for 3D textures
            .compareEnable = VK_FALSE,
            .minLod = 0.0f,
            .maxLod = filterOr(tg3Sampler.min_filter, VK_LOD_CLAMP_NONE,
                               [](const auto &t) { return std::get<2>(t); })
        };

        const uint32_t samplerId = m_resources.addSampler(samplerInfo);
        samplerIds[i] = samplerId ? samplerId : m_resources.defaultSamplerId();
    }
    return samplerIds;
}

std::vector<Image> GltfLoader::loadImages(const tg3_model &model,
                                          const std::filesystem::path &imageDir) const
{
    std::vector<Image> images(model.images_count);

    for (int i = 0; i < model.images_count; ++i) {
        Image &img = images[i];

        // Embedded/buffer-view images have no URI. Those aren't handled yet;
        // they fall through to the error texture rather than crashing.
        if (!model.images[i].uri.data) {
            showError("glTF image has no URI (embedded images are not supported yet)");
            continue;
        }

        const std::filesystem::path imagePath = imageDir / model.images[i].uri.data;
        std::cout << "Loading image " << i + 1 << " / " << model.images_count
                  << ": " << model.images[i].uri.data << std::endl;

        img.data = stbi_load(imagePath.string().c_str(), &img.width, &img.height, &img.channels, 4);
        if (!img.data) {
            showError("Failed to load image: " + imagePath.string());
        }
    }
    return images;
}

std::vector<uint32_t> GltfLoader::uploadImages(const std::vector<Image> &images)
{
    std::vector<uint32_t> imageIds(images.size());
    if (images.empty()) {
        return imageIds;
    }

    VkCommandBuffer commandBuffer = m_ctx.beginUpload();
    if (!commandBuffer) {
        for (uint32_t &id : imageIds) {
            id = m_resources.errorImageId();
        }
        return imageIds;
    }

    for (size_t i = 0; i < images.size(); ++i) {
        const Image &image = images[i];
        if (!image.data) {
            imageIds[i] = m_resources.errorImageId();
            continue;
        }

        const uint32_t imageId = m_resources.addImage(commandBuffer, image.data,
                                                      static_cast<uint32_t>(image.width),
                                                      static_cast<uint32_t>(image.height),
                                                      image.format,
                                                      AssetOrigin::Imported);

        imageIds[i] = imageId ? imageId : m_resources.errorImageId();
    }

    // Returns as soon as the copies are queued. Staging memory belongs to the
    // uploader now and is released when the GPU passes this ticket.
    m_ctx.submitUpload();
    return imageIds;
}