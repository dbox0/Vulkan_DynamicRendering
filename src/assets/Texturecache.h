#pragma once
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

class VulkanContext;
class ResourceStore;


class TextureCache
{
public:
    enum class Wrap : uint8_t { Repeat, ClampToEdge, MirroredRepeat };

    explicit TextureCache(std::filesystem::path assetRoot)
        : m_root(std::filesystem::absolute(std::move(assetRoot))) {}

    const std::filesystem::path &root() const { return m_root; }


    std::string toRelative(const std::filesystem::path &absolute) const;
    std::filesystem::path toAbsolute(std::string_view relative) const;

    // Decodes and uploads on a miss, returns the existing ID on a hit.
    // `cmd` must come from VulkanContext::beginUpload()
    // Returns 0 on failure.

    uint32_t acquireTexture(VulkanContext &ctx, ResourceStore &resources,
                            VkCommandBuffer cmd, std::string_view relativePath,
                            bool srgb, Wrap wrap = Wrap::Repeat);

    void registerImage(uint32_t imageId, std::string_view relativePath, bool srgb);

    // Reverse lookups, for saving. Empty / Repeat when unknown.
    std::string pathForImage(uint32_t imageId) const;
    Wrap        wrapForSampler(uint32_t samplerId) const;

    static const char *wrapName(Wrap wrap);
    static Wrap        wrapFromName(std::string_view name);

private:
    uint32_t acquireImage(VulkanContext &ctx, ResourceStore &resources,
                          VkCommandBuffer cmd, std::string_view relativePath, bool srgb);
    uint32_t acquireSampler(ResourceStore &resources, Wrap wrap);

    static std::string imageKey(std::string_view relativePath, bool srgb);

    std::filesystem::path m_root;

    std::unordered_map<std::string, uint32_t> m_imagesByKey;    // "rel|srgb"        -> imageId
    std::unordered_map<uint32_t, std::string> m_pathsByImage;   // imageId           -> "rel"
    std::unordered_map<uint64_t, uint32_t>    m_texturesByPair; // imageId:samplerId -> textureId
    std::unordered_map<uint8_t, uint32_t>     m_samplersByWrap; // Wrap              -> samplerId
    std::unordered_map<uint32_t, uint8_t>     m_wrapBySampler;  // samplerId         -> Wrap
};