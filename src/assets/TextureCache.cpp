#include "TextureCache.h"

#include <volk.h>
#include "../../third_party/stb_image.h"
#include <system_error>

#include "../render/core/VulkanContext.h"
#include "../render/resources/ResourceStore.h"
#include "../common/errors.h"

namespace
{
    constexpr uint64_t pairKey(uint32_t imageId, uint32_t samplerId)
    {
        return (static_cast<uint64_t>(imageId) << 32) | samplerId;
    }
}

const char *TextureCache::wrapName(Wrap wrap)
{
    switch (wrap) {
    case Wrap::ClampToEdge:    return "clamp";
    case Wrap::MirroredRepeat: return "mirror";
    case Wrap::Repeat:
    default:                   return "repeat";
    }
}

TextureCache::Wrap TextureCache::wrapFromName(std::string_view name)
{
    if (name == "clamp")  return Wrap::ClampToEdge;
    if (name == "mirror") return Wrap::MirroredRepeat;
    return Wrap::Repeat;
}

std::string TextureCache::imageKey(std::string_view relativePath, bool srgb)
{
    std::string key(relativePath);
    key += srgb ? "|s" : "|l";
    return key;
}

std::string TextureCache::toRelative(const std::filesystem::path &absolute) const
{
    // Empty means "no file" (an in-editor or glTF-embedded material). The
    // throwing overload of absolute() rejects an empty path, so both calls
    // take an error_code: nothing on this path may throw.
    if (absolute.empty()) {
        return {};
    }

    std::error_code ec;
    const std::filesystem::path full = std::filesystem::absolute(absolute, ec);
    if (ec) {
        return {};
    }
    const std::filesystem::path rel = std::filesystem::relative(full, m_root, ec);

    // Outside the asset root. Writing an absolute path here would produce a
    // .mat that only loads on this machine, so refuse instead.
    if (ec || rel.empty() || rel.generic_string().starts_with("..")) {
        return {};
    }
    return rel.generic_string();
}

std::filesystem::path TextureCache::toAbsolute(std::string_view relative) const
{
    return m_root / std::filesystem::path(relative);
}

void TextureCache::registerImage(uint32_t imageId, std::string_view relativePath, bool srgb)
{
    if (!imageId || relativePath.empty()) {
        return;
    }
    m_imagesByKey.emplace(imageKey(relativePath, srgb), imageId);
    m_pathsByImage.emplace(imageId, std::string(relativePath));
}

std::string TextureCache::pathForImage(uint32_t imageId) const
{
    const auto it = m_pathsByImage.find(imageId);
    return it == m_pathsByImage.end() ? std::string{} : it->second;
}

TextureCache::Wrap TextureCache::wrapForSampler(uint32_t samplerId) const
{
    const auto it = m_wrapBySampler.find(samplerId);
    return it == m_wrapBySampler.end() ? Wrap::Repeat : static_cast<Wrap>(it->second);
}

uint32_t TextureCache::acquireImage(VulkanContext &ctx, ResourceStore &resources,
                                    VkCommandBuffer cmd, std::string_view relativePath, bool srgb)
{
    const std::string key = imageKey(relativePath, srgb);
    if (const auto it = m_imagesByKey.find(key); it != m_imagesByKey.end()) {
        return it->second;
    }

    const std::filesystem::path absolute = toAbsolute(relativePath);

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc *pixels = stbi_load(absolute.string().c_str(), &width, &height, &channels, 4);
    if (!pixels) {
        showError("TextureCache: failed to decode " + absolute.string());
        return 0;
    }

    // The slot decides the colour space, not the file
    const VkFormat format = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;

    const uint32_t imageId = resources.addImage(cmd, pixels,
                                                static_cast<uint32_t>(width),
                                                static_cast<uint32_t>(height), format);
    stbi_image_free(pixels);

    if (!imageId) {
        showError("TextureCache: failed to upload " + absolute.string());
        return 0;
    }

    resources.setImageName(imageId, std::string(relativePath));
    m_imagesByKey.emplace(key, imageId);
    m_pathsByImage.emplace(imageId, std::string(relativePath));
    return imageId;
}

uint32_t TextureCache::acquireSampler(ResourceStore &resources, Wrap wrap)
{
    const auto wrapKey = static_cast<uint8_t>(wrap);
    if (const auto it = m_samplersByWrap.find(wrapKey); it != m_samplersByWrap.end()) {
        return it->second;
    }

    VkSamplerAddressMode mode = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    switch (wrap) {
    case Wrap::ClampToEdge:    mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;   break;
    case Wrap::MirroredRepeat: mode = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT; break;
    case Wrap::Repeat:         break;
    }

    // LOD_CLAMP_NONE because createImage2D generates the full chain it can.
    VkSamplerCreateInfo samplerInfo
    {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = mode,
        .addressModeV = mode,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .compareEnable = VK_FALSE,
        .minLod = 0.0f,
        .maxLod = VK_LOD_CLAMP_NONE
    };

    uint32_t samplerId = resources.addSampler(samplerInfo);
    if (!samplerId) {
        samplerId = resources.defaultSamplerId();
    }

    m_samplersByWrap.emplace(wrapKey, samplerId);
    m_wrapBySampler.emplace(samplerId, wrapKey);
    return samplerId;
}

uint32_t TextureCache::acquireTexture(VulkanContext &ctx, ResourceStore &resources,
                                      VkCommandBuffer cmd, std::string_view relativePath,
                                      bool srgb, Wrap wrap)
{
    if (relativePath.empty()) {
        return 0;
    }

    const uint32_t imageId = acquireImage(ctx, resources, cmd, relativePath, srgb);
    if (!imageId) {
        return 0;
    }

    const uint32_t samplerId = acquireSampler(resources, wrap);
    const uint64_t key = pairKey(imageId, samplerId);

    if (const auto it = m_texturesByPair.find(key); it != m_texturesByPair.end()) {
        return it->second;
    }

    const uint32_t textureId = resources.addTexture(imageId, samplerId);
    if (!textureId) {
        return 0;
    }

    m_texturesByPair.emplace(key, textureId);
    return textureId;
}
