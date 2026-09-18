#include "ResourceStore.h"

#include <algorithm>
#include <volk.h>
#include <vk_mem_alloc.h>
#include <array>
#include <cstring>
#include <utility>
#include <vector>

#include "stb_image.h"
#include "../core/VulkanContext.h"
#include "../../common/errors.h"
#include "../../common/constants.h"

uint16_t floatToHalf(float value)
{
    // Half tops out at 65504. HDR panoramas routinely store suns well past
    // that, and an inf here turns into NaN pixels the moment it hits the
    // specular term, so clamp rather than let it overflow.
    value = std::clamp(value, -65504.0f, 65504.0f);

    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));

    const uint32_t sign     = (bits >> 16) & 0x8000u;
    const int32_t  exponent = static_cast<int32_t>((bits >> 23) & 0xFFu) - 127 + 15;
    const uint32_t mantissa = bits & 0x007FFFFFu;

    if (exponent >= 31) {
        return static_cast<uint16_t>(sign | 0x7C00u | (mantissa ? 0x200u : 0u));
    }
    if (exponent <= 0) {
        return static_cast<uint16_t>(sign);
    }
    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exponent) << 10) | (mantissa >> 13));
}

bool ResourceStore::initialize()
{
    if (!createDescriptorSets()) {
        return false;
    }
    // Before the default textures: addImage() records bookkeeping, and
    // addMaterial() writes through the mapped pointer, so the buffer has to
    // exist first.
    if (!createMaterialBuffer()) {
        return false;
    }
    if (!createDefaultTextures()) {
        return false;
    }
    if (!createDefaultMaterial()) {
        return false;
    }
    // Commit slots 0 and 1 immediately so the descriptor array is valid even
    // if no model is ever loaded. An empty scene now renders without
    // validation errors.
    return commitTextureDescriptors();
}

void ResourceStore::shutdown()
{
    if (!m_ctx.device()) {
        return;
    }

    if (m_materialPtr) {
        vmaUnmapMemory(m_ctx.allocator(), m_materialBuffer.allocation);
        m_materialPtr = nullptr;
    }
    m_ctx.destroyBuffer(m_materialBuffer);

    if (m_globalLayout) {
        vkDestroyDescriptorSetLayout(m_ctx.device(), m_globalLayout, nullptr);
        m_globalLayout = nullptr;
    }
    if (m_descriptorPool) {
        // Frees m_globalDescSet implicitly.
        vkDestroyDescriptorPool(m_ctx.device(), m_descriptorPool, nullptr);
        m_descriptorPool = nullptr;
        m_globalDescSet = nullptr;
    }

    for (GPUImage &img : m_images) {
        m_ctx.destroyImage(img);
    }
    m_images.clear();
    m_imageInfos.clear();

    for (VkSampler sampler : m_samplers) {
        vkDestroySampler(m_ctx.device(), sampler, nullptr);
    }
    m_samplers.clear();

    for (GPUBuffer &buff : m_buffers) {
        m_ctx.destroyBuffer(buff);
    }
    m_buffers.clear();

    m_textures.clear();
    m_materials.clear();
    m_texturesWritten = 0;
}


// ============================================================================
// adders -- 1-based IDs, 0 means failure/none


uint32_t ResourceStore::addImage(VkCommandBuffer commandBuffer, const void *data,
                                 uint32_t width, uint32_t height, VkFormat format,
                                 AssetOrigin origin)
{
    GPUImage gpuImage;
    if (!m_ctx.createImage2D(commandBuffer, data, width, height, format, gpuImage)) {
        return 0;
    }
    m_images.push_back(gpuImage);


    m_imageInfos.push_back(ImageInfo{
        .width = width, .height = height,
        .mipLevels = gpuImage.mipLevels,
        .format = format,
        .origin = origin });
    return static_cast<uint32_t>(m_images.size());
}

uint32_t ResourceStore::addSampler(const VkSamplerCreateInfo &info)
{
    VkSampler sampler = nullptr;
    if (vkCreateSampler(m_ctx.device(), &info, nullptr, &sampler) != VK_SUCCESS) {
        showError("Unable to create texture sampler");
        return 0;
    }
    m_samplers.push_back(sampler);
    return static_cast<uint32_t>(m_samplers.size());
}

uint32_t ResourceStore::addTexture(uint32_t imageId, uint32_t samplerId)
{
    if (m_textures.size() >= MaxTextures) {
        showError("Exceeded the maximum texture count");
        return m_errorTextureId;
    }

    // A texture asked for an image that does not exist -- that is broken, not
    // missing, so it gets magenta rather than white.
    if (imageId == 0 || imageId > m_images.size()) {
        imageId = m_errorImageId;
    }
    if (samplerId == 0 || samplerId > m_samplers.size()) {
        samplerId = m_defaultSamplerId;
    }

    m_textures.push_back(Texture{ .imageId = imageId, .samplerId = samplerId });
    return static_cast<uint32_t>(m_textures.size());
}

uint32_t ResourceStore::addBuffer(const GPUBuffer &buffer)
{
    m_buffers.push_back(buffer);
    return static_cast<uint32_t>(m_buffers.size());
}


// ============================================================================
// Env
// ============================================================================

uint32_t ResourceStore::loadEnvironment(const std::filesystem::path &path)
{
    int width = 0, height = 0, channels = 0;
    float *pixels = stbi_loadf(path.string().c_str(), &width, &height, &channels, 4);
    if (!pixels) {
        std::cerr << "[warn] Unable to load environment map " << path
                  << ": " << stbi_failure_reason() << std::endl;
        return 0;
    }

    const size_t componentCount = static_cast<size_t>(width) * height * 4;
    std::vector<uint16_t> halfPixels(componentCount);

    VkCommandBuffer cmd = m_ctx.beginUpload();
    if (!cmd) {
        return 0;
    }

    // 16F is exactly the format most likely to come back without
    // SAMPLED_IMAGE_FILTER_LINEAR, in which case this lands as a single
    // level and environmentMaxLod() follows it down.
    const uint32_t imageId = addImage(cmd, pixels,
                                      static_cast<uint32_t>(width),
                                      static_cast<uint32_t>(height),
                                      VK_FORMAT_R32G32B32A32_SFLOAT,
                                      AssetOrigin::Builtin);
    m_ctx.submitUpload();

    if (!imageId) {
        return 0;
    }
    setImageName(imageId, path.filename().string());

    if (!m_envSamplerId) {
        // Repeat in U so the horizontal seam wraps; clamp in V because
        // repeating would mirror the sky across the poles.
        VkSamplerCreateInfo samplerInfo
        {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_LINEAR,
            .minFilter = VK_FILTER_LINEAR,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .compareEnable = VK_FALSE,
            .minLod = 0.0f,
            .maxLod = VK_LOD_CLAMP_NONE
        };
        m_envSamplerId = addSampler(samplerInfo);
    }

    m_envTextureId = addTexture(imageId, m_envSamplerId);
    return m_envTextureId;
}

// ============================================================================
// materials
// ============================================================================

bool ResourceStore::createMaterialBuffer()
{
    const size_t bytes = MaxMaterials * sizeof(GpuMaterial);

    // Host-visible and kept mapped. ~288KB at 4096 materials -- comfortably
    // inside the BAR window, and it means the editor can tweak a material
    // without touching a transfer queue. Move it device-local with a staged
    // copy only if profiling ever says the shader read is the problem.
    m_materialBuffer = m_ctx.createBuffer(
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        bytes, true, VMA_MEMORY_USAGE_AUTO);

    if (!m_materialBuffer.vkBuffer) {
        showError("Error creating the material buffer");
        return false;
    }

    void *mapped = nullptr;
    if (vmaMapMemory(m_ctx.allocator(), m_materialBuffer.allocation, &mapped) != VK_SUCCESS) {
        showError("Unable to map the material buffer");
        m_ctx.destroyBuffer(m_materialBuffer);
        return false;
    }
    m_materialPtr = static_cast<GpuMaterial *>(mapped);

    // Reserved so the CPU mirror never reallocates; material() and
    // materialInfo() hand out references and the editor holds them across a
    // frame.
    m_materials.reserve(MaxMaterials);
    m_materialInfos.reserve(MaxMaterials);
    return true;
}

GpuMaterial ResourceStore::toGpu(const Material &m) const
{
    // 0 = "this material has no such texture" -> white, which is the identity
    // for every slot that gets multiplied by a factor. Anything non-zero goes
    // through textureDescriptorSlot, which falls back to the ERROR slot: a
    // texture ID that does not resolve is a bug worth seeing.
    auto slot = [this](uint32_t textureId) {
        return textureId ? textureDescriptorSlot(textureId)
                         : (m_whiteTextureId ? m_whiteTextureId - 1 : 0);
    };

    uint32_t flags = 0;
    if (m.alphaMode == AlphaMode::Mask)  flags |= MaterialFlag_AlphaMask;
    if (m.alphaMode == AlphaMode::Blend) flags |= MaterialFlag_AlphaBlend;
    if (m.doubleSided)                   flags |= MaterialFlag_DoubleSided;
    // Only set when a normal map actually exists: the shader skips the whole
    // tangent-frame branch otherwise, so there is no "neutral normal" default
    // texture to maintain.
    if (m.normalTexture)                 flags |= MaterialFlag_NormalMap;

    return GpuMaterial
    {
        .baseColorFactor      = m.baseColorFactor,
        // Folded here so the shader does one multiply less per fragment.
        .emissiveFactor       = m.emissiveFactor * m.emissiveStrength,
        .metallicFactor       = m.metallicFactor,
        .roughnessFactor      = m.roughnessFactor,
        .normalScale          = m.normalScale,
        .occlusionStrength    = m.occlusionStrength,
        .alphaCutoff          = m.alphaCutoff,
        .baseColorTex         = slot(m.baseColorTexture),
        .metallicRoughnessTex = slot(m.metallicRoughnessTexture),
        .normalTex            = slot(m.normalTexture),
        .occlusionTex         = slot(m.occlusionTexture),
        .emissiveTex          = slot(m.emissiveTexture),
        .flags                = flags
    };
}

uint32_t ResourceStore::addMaterial(const Material &material, AssetOrigin origin)
{
    if (m_materials.size() >= MaxMaterials) {
        showError("Exceeded the maximum material count");
        return 0;
    }
    if (!m_materialPtr) {
        showError("addMaterial before initialize()");
        return 0;
    }

    const size_t index = m_materials.size();
    m_materials.push_back(material);
    m_materialInfos.emplace_back();
    m_materialInfos.back().origin = origin;
    m_materialPtr[index] = toGpu(material);

    // No-op on coherent memory, required if VMA hands back non-coherent.
    vmaFlushAllocation(m_ctx.allocator(), m_materialBuffer.allocation,
                       index * sizeof(GpuMaterial), sizeof(GpuMaterial));

    return static_cast<uint32_t>(index + 1);
}

bool ResourceStore::updateMaterial(uint32_t materialId, const Material &material)
{
    if (!materialId || materialId > m_materials.size() || !m_materialPtr) {
        showError("updateMaterial with an invalid material ID");
        return false;
    }

    const size_t index = materialId - 1;
    m_materials[index] = material;
    m_materialInfos[index].dirty = true;
    m_materialPtr[index] = toGpu(material);

    vmaFlushAllocation(m_ctx.allocator(), m_materialBuffer.allocation,
                       index * sizeof(GpuMaterial), sizeof(GpuMaterial));
    return true;
}

uint32_t ResourceStore::textureDescriptorSlot(uint32_t textureId) const
{
    // Descriptor array is 0-based; our IDs are 1-based. This is the ONLY
    // place that conversion is allowed to happen.
    if (textureId == 0 || textureId > m_textures.size()) {
        return m_errorTextureId ? m_errorTextureId - 1 : 0;
    }
    return textureId - 1;
}


// ============================================================================
// defaults
// ============================================================================

bool ResourceStore::createDefaultTextures()
{
    // ORDER MATTERS. White must be texture ID 1 (descriptor slot 0) so a
    // zero-initialised GpuMaterial is always safe to sample, and magenta
    // second so "broken" stays visually distinct from "not supplied".
    //
    // Byte arrays, not a packed uint32_t: 0xFF00FFFF is stored little-endian
    // as FF FF 00 FF, which uploads as YELLOW, not magenta.
    static constexpr uint8_t whitePixel[4]   = { 255, 255, 255, 255 };
    static constexpr uint8_t magentaPixel[4] = { 255,   0, 255, 255 };

    // UNORM, not SRGB: these are engine-authored constants, not decoded image
    // files, so there is no colour space to undo.
    constexpr VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;

    VkCommandBuffer cmd = m_ctx.beginUpload();
    if (!cmd) {
        return false;
    }

    m_whiteImageId = addImage(cmd, whitePixel,   1, 1, format, AssetOrigin::Builtin);
    m_errorImageId = addImage(cmd, magentaPixel, 1, 1, format, AssetOrigin::Builtin);

    // Both copies are recorded before the single submit, and the submit does
    // not block: the descriptor writes below do not read the pixels, and the
    // first frame that samples them is submitted after this on the same
    // queue, so the copies are already ordered ahead of it.
    m_ctx.submitUpload();

    if (!m_whiteImageId || !m_errorImageId) {
        showError("Unable to create the default images");
        return false;
    }
    setImageName(m_whiteImageId, "<white>");
    setImageName(m_errorImageId, "<missing>");

    VkSamplerCreateInfo samplerInfo
    {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .compareEnable = VK_FALSE,
        .minLod = 0.0f,
        .maxLod = VK_LOD_CLAMP_NONE
    };

    m_defaultSamplerId = addSampler(samplerInfo);
    if (!m_defaultSamplerId) {
        showError("Unable to create the default texture sampler");
        return false;
    }

    m_whiteTextureId = addTexture(m_whiteImageId, m_defaultSamplerId);
    m_errorTextureId = addTexture(m_errorImageId, m_defaultSamplerId);

    // The invariant the rest of the class depends on. If anything ever gets
    // added before these, every zeroed GpuMaterial silently samples the wrong
    // texture -- so fail loudly here instead.
    if (m_whiteTextureId != 1 || m_errorTextureId != 2) {
        showError("Default textures did not land in descriptor slots 0 and 1");
        return false;
    }
    return true;
}

bool ResourceStore::createDefaultMaterial()
{
    // What a submesh with materialId 0 renders as. Deliberately NOT the glTF
    // spec default (metallic 1, roughness 1), which reads as near-black
    // chrome without IBL -- this is meant to look like untextured plastic.
    Material def;
    def.name            = "Default";
    def.metallicFactor  = 0.0f;
    def.roughnessFactor = 0.5f;

    m_defaultMaterialId = addMaterial(def, AssetOrigin::Builtin);

    // Must be index 0, because Renderer maps materialId 0 to GPU index 0.
    if (m_defaultMaterialId != 1) {
        showError("Default material did not land at index 0");
        return false;
    }
    return true;
}

// ============================================================================
// bindless descriptors
// ============================================================================

VkDescriptorImageInfo ResourceStore::describeTexture(const Texture &t) const
{
    const bool imageOk   = t.imageId   > 0 && t.imageId   <= m_images.size();
    const bool samplerOk = t.samplerId > 0 && t.samplerId <= m_samplers.size();

    const GPUImage &image = imageOk
        ? m_images[t.imageId - 1]
        : m_images[m_errorImageId - 1];
    const VkSampler sampler = samplerOk
        ? m_samplers[t.samplerId - 1]
        : m_samplers[m_defaultSamplerId - 1];

    if (!imageOk || !samplerOk || !image.imageView) {
        showError("Texture references an invalid image or sampler; using the error texture");
        return VkDescriptorImageInfo
        {
            .sampler     = m_samplers[m_defaultSamplerId - 1],
            .imageView   = m_images[m_errorImageId - 1].imageView,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        };
    }

    return VkDescriptorImageInfo
    {
        .sampler     = sampler,
        .imageView   = image.imageView,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };
}

bool ResourceStore::commitTextureDescriptors()
{
    const uint32_t total = static_cast<uint32_t>(m_textures.size());
    if (total <= m_texturesWritten) {
        return true;                       // nothing new since last commit
    }

    const uint32_t first = m_texturesWritten;
    const uint32_t count = total - first;

    std::vector<VkDescriptorImageInfo> imageDescriptors;
    imageDescriptors.reserve(count);
    for (uint32_t i = first; i < total; ++i) {
        imageDescriptors.push_back(describeTexture(m_textures[i]));
    }

    VkWriteDescriptorSet write
    {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = m_globalDescSet,
        .dstBinding = 0,
        .dstArrayElement = first,
        .descriptorCount = count,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = imageDescriptors.data()
    };

    vkUpdateDescriptorSets(m_ctx.device(), 1, &write, 0, nullptr);
    m_texturesWritten = total;
    return true;
}

bool ResourceStore::replaceTextureDescriptor(uint32_t textureId)
{
    if (!textureId || textureId > m_textures.size()) {
        showError("replaceTextureDescriptor with an invalid texture ID");
        return false;
    }

    const uint32_t slot = textureId - 1;
    if (slot >= m_texturesWritten) {
        // Not committed yet -- commitTextureDescriptors() will pick it up.
        return true;
    }

    const VkDescriptorImageInfo info = describeTexture(m_textures[slot]);

    VkWriteDescriptorSet write
    {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = m_globalDescSet,
        .dstBinding = 0,
        .dstArrayElement = slot,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &info
    };

    vkUpdateDescriptorSets(m_ctx.device(), 1, &write, 0, nullptr);
    return true;
}

bool ResourceStore::createDescriptorSets()
{
    std::array<VkDescriptorPoolSize, 1> poolSizes
    {
        VkDescriptorPoolSize
        {
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = MaxTextures
        }
    };

    VkDescriptorPoolCreateInfo poolInfo
    {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        // Lets us rewrite descriptors after they have been bound.
        .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
        .maxSets = 1,
        .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
        .pPoolSizes = poolSizes.data()
    };

    if (vkCreateDescriptorPool(m_ctx.device(), &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        showError("Unable to create descriptor pool");
        return false;
    }

    std::array<VkDescriptorSetLayoutBinding, 1> bindings
    {
        VkDescriptorSetLayoutBinding
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = MaxTextures,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
        }
    };

    std::array<VkDescriptorBindingFlags, 1> flags
    {
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT
    };

    VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo
    {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .bindingCount = static_cast<uint32_t>(flags.size()),
        .pBindingFlags = flags.data()
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo
    {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = &flagsInfo,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
        .bindingCount = static_cast<uint32_t>(bindings.size()),
        .pBindings = bindings.data()
    };

    if (vkCreateDescriptorSetLayout(m_ctx.device(), &layoutInfo, nullptr, &m_globalLayout) != VK_SUCCESS) {
        showError("Unable to create descriptor set layout");
        return false;
    }

    VkDescriptorSetAllocateInfo descSetAllocInfo
    {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = m_descriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts = &m_globalLayout
    };

    if (vkAllocateDescriptorSets(m_ctx.device(), &descSetAllocInfo, &m_globalDescSet) != VK_SUCCESS) {
        showError("Unable to allocate descriptor set");
        return false;
    }
    return true;
}