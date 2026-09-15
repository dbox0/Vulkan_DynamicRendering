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

// Path -> texture ID, with dedup at all three levels ResourceStore already
// splits things into (image / sampler / texture).
//
// This exists because Material stores a 1-based ResourceStore texture ID,
// which is a runtime index and means nothing across sessions. Serializing a
// material therefore needs stable texture identity, and the cheapest stable
// identity is the file it came from. That makes this the first real piece of
// an asset database, so it also keeps the REVERSE map: given an image ID,
// which file produced it. saveMaterial() needs exactly that.
//
// Paths are stored relative to the asset root with forward slashes, so a
// .mat file is portable between machines and OSes.
//
// OWNERSHIP: none. Everything here is an ID into ResourceStore, which owns the
// actual Vulkan objects and destroys them in shutdown(). This class only
// remembers which ID belongs to which file.
class TextureCache
{
public:
    enum class Wrap : uint8_t { Repeat, ClampToEdge, MirroredRepeat };

    explicit TextureCache(std::filesystem::path assetRoot)
        : m_root(std::filesystem::absolute(std::move(assetRoot))) {}

    const std::filesystem::path &root() const { return m_root; }

    // Forward slashes, relative to the root. Empty when `absolute` is outside
    // the root -- an asset the project cannot refer to portably, which the
    // caller should treat as an error rather than writing an absolute path.
    std::string toRelative(const std::filesystem::path &absolute) const;
    std::filesystem::path toAbsolute(std::string_view relative) const;

    // Decodes and uploads on a miss, returns the existing ID on a hit.
    //
    // `cmd` must come from VulkanContext::beginUpload(). The caller brackets
    // begin/submit so a batch -- five slots of one material, or a dozen
    // materials dropped at once -- shares a single submission, the same way
    // GltfLoader::uploadImages does for one file.
    //
    // Returns 0 on failure. Callers substitute errorTextureId(): "missing" and
    // "broken" are different states, and a material pointing at a deleted PNG
    // should be loudly magenta, not silently white.
    uint32_t acquireTexture(VulkanContext &ctx, ResourceStore &resources,
                            VkCommandBuffer cmd, std::string_view relativePath,
                            bool srgb, Wrap wrap = Wrap::Repeat);

    // Records provenance for an image this cache did not load. GltfLoader
    // should call it for every image it uploads, otherwise a material imported
    // from a .gltf cannot be saved -- its texture IDs resolve to images with
    // no known source file.
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

    // The colour space is part of the key, not a property of the file: the
    // same PNG is legitimately loaded twice when one material uses it as a
    // base colour (_SRGB) and another as a mask (_UNORM). Keying on path alone
    // would hand the second one a texture decoded in the wrong space.
    static std::string imageKey(std::string_view relativePath, bool srgb);

    std::filesystem::path m_root;

    std::unordered_map<std::string, uint32_t> m_imagesByKey;    // "rel|srgb"        -> imageId
    std::unordered_map<uint32_t, std::string> m_pathsByImage;   // imageId           -> "rel"
    std::unordered_map<uint64_t, uint32_t>    m_texturesByPair; // imageId:samplerId -> textureId
    std::unordered_map<uint8_t, uint32_t>     m_samplersByWrap; // Wrap              -> samplerId
    std::unordered_map<uint32_t, uint8_t>     m_wrapBySampler;  // samplerId         -> Wrap
};
