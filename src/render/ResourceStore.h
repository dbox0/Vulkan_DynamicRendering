#pragma once
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>
#include <string>
#include "../assets/Material.h"
#include "../common/gpu_types.h"
#include "GpuShared.h"
#include <volk.h>
#include <filesystem>
#include <iostream>

class VulkanContext;

// Where an asset came from. The Project panel filters on this, and it is the
// only way to tell a material that belongs to the project from one that came
// in attached to a model -> inspired by Unity: draws the same line between a material asset
// and a model-embedded material, and for the same reason: you can edit the
// first and you are fighting the importer on the second.


enum class AssetOrigin : uint8_t
{
    Builtin,    // white / error / environment textures, the default material
    Imported,   // created by GltfLoader as part of a model
    Project     // created in the editor, or loaded from a standalone file
};

// Owns every bindless GPU resource: images, samplers, textures, materials and
// raw buffers, plus the global descriptor set the fragment shader samples
// through.
//
// ID CONVENTION
//   * Returned IDs are 1-based:  id == vector index + 1.  id 0 == "none".
//   * Texture slot 0 (texture ID 1) is ALWAYS the white default, so a
//     zero-initialised GpuMaterial samples something harmless.
//   * Texture slot 1 (texture ID 2) is the magenta error texture, used only
//     for genuinely broken references -- never for "this material has no
//     such map".
//   * Material ID 1 is the engine default material, so submeshes with
//     materialId 0 map to GPU index 0 and render grey rather than picking up
//     whichever material happened to load first.
//
// MATERIALS COME IN TWO FLAVOURS
//   * Material    (assets/Material.h)  -- authoring. Holds names, enums and
//                                         1-based texture IDs. Editable.
//   * GpuMaterial (render/GpuShared.h) -- packed, matches the shader struct.
//     toGpu() is the ONLY conversion between them, and the only place texture
//     IDs become descriptor slots.

class ResourceStore
{
public:
    explicit ResourceStore(VulkanContext &ctx) : m_ctx(ctx) {}
    ResourceStore(const ResourceStore &) = delete;
    ResourceStore &operator=(const ResourceStore &) = delete;

    // Bookkeeping the editor needs to preview a texture. Parallel to m_images.
    struct ImageInfo
    {
        std::string name;
        uint32_t    width  = 0;
        uint32_t    height = 0;
        uint32_t    mipLevels = 1;
        VkFormat    format    = VK_FORMAT_UNDEFINED;
        AssetOrigin origin    = AssetOrigin::Project;
    };

    struct Texture
    {
        uint32_t imageId   = 0;
        uint32_t samplerId = 0;
    };

    // Where a material came from and whether it has been edited since. Kept
    // parallel to m_materials rather than folded into Material, because
    // Material is the authoring payload toGpu() packs and saveMaterial()
    // writes -- where it came from is a different concern, and keeping it out
    // means a Material value stays cheap to copy (the inspector edits one).
    struct MaterialInfo
    {
        std::filesystem::path sourcePath;   // empty = created in-editor, never saved
        bool        dirty  = false;
        AssetOrigin origin = AssetOrigin::Project;
    };

    bool initialize();          // descriptors + material buffer + defaults
    void shutdown();

    // --- images / samplers / textures ------------------------------------
    // Records the upload into commandBuffer, which must come from
    // VulkanContext::beginUpload(). Staging is owned by the uploader, so
    // there is nothing for the caller to free and nothing to wait on.
    // Returns id 0 on failure.
    //
    // format is the caller's decision, not this class's: glTF images carry no
    // colour space, so only the material slot sampling an image knows whether
    // it is colour (_SRGB) or data (_UNORM).

    uint32_t addImage(VkCommandBuffer commandBuffer, const void *data,
                      uint32_t width, uint32_t height, VkFormat format,
                      AssetOrigin origin = AssetOrigin::Project);

    uint32_t addSampler(const VkSamplerCreateInfo &info);
    uint32_t addTexture(uint32_t imageId, uint32_t samplerId);
    uint32_t addBuffer(const GPUBuffer &buffer);

    // "Missing" and "broken" are different states and get different textures.
    uint32_t whiteTextureId()    const { return m_whiteTextureId; }
    uint32_t errorTextureId()    const { return m_errorTextureId; }
    uint32_t errorImageId()      const { return m_errorImageId; }
    uint32_t defaultSamplerId()  const { return m_defaultSamplerId; }
    uint32_t defaultMaterialId() const { return m_defaultMaterialId; }

    const GPUBuffer &buffer(uint32_t bufferId) const { return m_buffers[bufferId - 1]; }

    // --- read access, for the editor inspector ---------------------------
    size_t           imageCount() const                 { return m_images.size(); }
    size_t           textureCount() const               { return m_textures.size(); }
    const Texture   &texture(uint32_t textureId) const  { return m_textures[textureId - 1]; }
    const ImageInfo &imageInfo(uint32_t imageId) const  { return m_imageInfos[imageId - 1]; }
    VkImageView      imageView(uint32_t imageId) const  { return m_images[imageId - 1].imageView; }
    VkSampler        sampler(uint32_t samplerId) const  { return m_samplers[samplerId - 1]; }

    // glTF images have no name of their own; the loader passes the URI.
    void setImageName(uint32_t imageId, std::string name)
    {
        if (imageId && imageId <= m_imageInfos.size()) {
            m_imageInfos[imageId - 1].name = std::move(name);
        }
    }

    // --- materials -------------------------------------------------------
    // Stores the authoring copy and writes the packed GpuMaterial through to
    // the mapped buffer

    uint32_t addMaterial(const Material &material,
                         AssetOrigin origin = AssetOrigin::Project);

    // In-place edit, for the editor's material inspector. Safe at any time

    bool updateMaterial(uint32_t materialId, const Material &material);

    const Material &material(uint32_t materialId) const { return m_materials[materialId - 1]; }

    // A texture has no origin of its own: it is an image plus a sampler, and
    // the image is the thing that came from somewhere.
    AssetOrigin textureOrigin(uint32_t textureId) const
    {
        const uint32_t imageId = m_textures[textureId - 1].imageId;
        return imageId && imageId <= m_imageInfos.size()
                   ? m_imageInfos[imageId - 1].origin
                   : AssetOrigin::Builtin;
    }
    size_t materialCount() const { return m_materials.size(); }

    const MaterialInfo &materialInfo(uint32_t materialId) const
    {
        return m_materialInfos[materialId - 1];
    }

    // Called after a successful save or load, where the file on disk now
    // matches memory -> clearDirty. A rename passes false: the path
    // changed, the contents did not, and an unsaved edit is still unsaved.
    void setMaterialSource(uint32_t materialId, std::filesystem::path path,
                           bool clearDirty = true)
    {
        if (materialId && materialId <= m_materialInfos.size()) {
            m_materialInfos[materialId - 1].sourcePath = std::move(path);
            if (clearDirty) {
                m_materialInfos[materialId - 1].dirty = false;
            }
        }
    }
    uint64_t materialBufferAddress() const { return m_materialBuffer.deviceAddress; }


    // --- environment -------------------------------------------------

    uint32_t loadEnvironment(const std::filesystem::__cxx11::path &path);

    uint32_t environmentTextureId() const { return m_envTextureId; }
    uint32_t imageMipLevels(uint32_t imageId) const { return m_imageInfos[imageId - 1].mipLevels; }


    // --- bindless descriptors --------------------------------------------
    // Writes only the texture slots added since the last call.

    bool commitTextureDescriptors();

    // Rewrites one already-committed slot (swapping an image on a live
    // material). The caller MUST ensure no submitted frame is still reading
    // it -- vkDeviceWaitIdle, or defer this behind MaxFramesInFlight.
    bool replaceTextureDescriptor(uint32_t textureId);

    VkDescriptorSet       globalDescriptorSet() const { return m_globalDescSet; }
    VkDescriptorSetLayout globalLayout()        const { return m_globalLayout; }

private:
    bool createDescriptorSets();
    bool createMaterialBuffer();
    bool createDefaultTextures();
    bool createDefaultMaterial();

    // Authoring -> GPU. Resolves texture IDs to descriptor slots and packs
    // the alpha mode / double-sided / normal-map booleans into flags.

    GpuMaterial toGpu(const Material &material) const;

    // Converts a 1-based texture ID to the 0-based descriptor array slot the
    // shader uses. Private on purpose: toGpu() is the only caller, so nothing
    // outside this class ever handles a raw slot.

    uint32_t textureDescriptorSlot(uint32_t textureId) const;

    // Builds the VkDescriptorImageInfo for one texture, substituting the
    // error texture for anything malformed.
    VkDescriptorImageInfo describeTexture(const Texture &t) const;

    VulkanContext &m_ctx;

    std::vector<GPUImage>  m_images;
    std::vector<ImageInfo> m_imageInfos;  // parallel to m_images
    std::vector<VkSampler> m_samplers;
    std::vector<Texture>   m_textures;
    std::vector<Material>  m_materials;   // CPU mirror, for the inspector
    std::vector<MaterialInfo> m_materialInfos;  // parallel to m_materials
    std::vector<GPUBuffer> m_buffers;

    uint32_t m_whiteImageId     = 0;
    uint32_t m_errorImageId     = 0;
    uint32_t m_defaultSamplerId = 0;
    uint32_t m_whiteTextureId   = 0;
    uint32_t m_errorTextureId   = 0;
    uint32_t m_defaultMaterialId = 0;
    uint32_t m_envTextureId = 0;
    uint32_t m_envSamplerId = 0;

    // How many descriptor slots have actually been written. Everything below
    // this index is potentially in use by an in-flight frame!
    uint32_t m_texturesWritten = 0;

    GPUBuffer m_materialBuffer;
    GpuMaterial *m_materialPtr = nullptr;   // persistent map

    VkDescriptorPool      m_descriptorPool = nullptr;
    VkDescriptorSetLayout m_globalLayout   = nullptr;
    VkDescriptorSet       m_globalDescSet  = nullptr;
};