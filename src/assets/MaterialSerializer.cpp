#include "MaterialSerializer.h"

#include <volk.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <string>

#include "Material.h"
#include "TextureCache.h"
#include "../render/ResourceStore.h"
#include "../render/VulkanContext.h"
#include "../common/errors.h"

using nlohmann::json;

namespace
{
    constexpr int kFormatVersion = 1;

    const char *alphaModeName(AlphaMode mode)
    {
        switch (mode) {
        case AlphaMode::Mask:  return "Mask";
        case AlphaMode::Blend: return "Blend";
        case AlphaMode::Opaque:
        default:               return "Opaque";
        }
    }

    AlphaMode alphaModeFromName(const std::string &name)
    {
        if (name == "Mask")  return AlphaMode::Mask;
        if (name == "Blend") return AlphaMode::Blend;
        return AlphaMode::Opaque;
    }

    // Absent key -> leave the Material's own default in place. This is what
    // makes adding a field a non-event for existing files on disk.
    template <typename T>
    void readIfPresent(const json &j, const char *key, T &out)
    {
        if (const auto it = j.find(key); it != j.end() && !it->is_null()) {
            out = it->get<T>();
        }
    }

    void readVec3(const json &j, const char *key, glm::vec3 &out)
    {
        const auto it = j.find(key);
        if (it != j.end() && it->is_array() && it->size() >= 3) {
            out = glm::vec3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
        }
    }

    void readVec4(const json &j, const char *key, glm::vec4 &out)
    {
        const auto it = j.find(key);
        if (it != j.end() && it->is_array() && it->size() >= 4) {
            out = glm::vec4((*it)[0].get<float>(), (*it)[1].get<float>(),
                            (*it)[2].get<float>(), (*it)[3].get<float>());
        }
    }

    // Texture ID -> { path, srgb, wrap }. Writes nothing for slot 0.
    // Returns false when the texture exists but its image has no known source,
    // which would silently drop a map on save.
    bool writeSlot(json &slots, const char *key, uint32_t textureId,
                   const ResourceStore &resources, const TextureCache &cache,
                   std::string &outError)
    {
        if (textureId == 0 || textureId > resources.textureCount()) {
            return true;
        }

        const ResourceStore::Texture &texture = resources.texture(textureId);
        const std::string relativePath = cache.pathForImage(texture.imageId);
        if (relativePath.empty()) {
            outError = std::string("texture slot '") + key +
                       "' comes from an image with no known source file";
            return false;
        }

        // Colour space is recovered from the format the image was actually
        // created with, so a round trip cannot silently flip a normal map into
        // sRGB.
        const bool srgb = resources.imageInfo(texture.imageId).format == VK_FORMAT_R8G8B8A8_SRGB;

        json slot;
        slot["path"] = relativePath;
        slot["srgb"] = srgb;

        const TextureCache::Wrap wrap = cache.wrapForSampler(texture.samplerId);
        if (wrap != TextureCache::Wrap::Repeat) {
            slot["wrap"] = TextureCache::wrapName(wrap);
        }

        slots[key] = std::move(slot);
        return true;
    }

    uint32_t readSlot(const json &slots, const char *key,
                      VulkanContext &ctx, ResourceStore &resources, TextureCache &cache,
                      VkCommandBuffer cmd)
    {
        const auto it = slots.find(key);
        if (it == slots.end() || !it->is_object()) {
            return 0;       // no map -> white default, not the error texture
        }

        std::string path;
        readIfPresent(*it, "path", path);
        if (path.empty()) {
            return 0;
        }

        bool srgb = false;
        readIfPresent(*it, "srgb", srgb);

        std::string wrapName = "repeat";
        readIfPresent(*it, "wrap", wrapName);

        const uint32_t textureId = cache.acquireTexture(ctx, resources, cmd, path, srgb,
                                                        TextureCache::wrapFromName(wrapName));

        // Named but unloadable is a broken reference, and broken is magenta.
        return textureId ? textureId : resources.errorTextureId();
    }
}

bool saveMaterial(const ResourceStore &resources, const TextureCache &cache,
                  uint32_t materialId, const std::filesystem::path &file)
{
    if (!materialId || materialId > resources.materialCount()) {
        showError("saveMaterial: invalid material ID");
        return false;
    }

    const Material &mat = resources.material(materialId);

    json doc;
    doc["version"]           = kFormatVersion;
    doc["name"]              = mat.name;
    doc["baseColorFactor"]   = { mat.baseColorFactor.r, mat.baseColorFactor.g,
                                 mat.baseColorFactor.b, mat.baseColorFactor.a };
    doc["emissiveFactor"]    = { mat.emissiveFactor.r, mat.emissiveFactor.g, mat.emissiveFactor.b };
    doc["emissiveStrength"]  = mat.emissiveStrength;
    doc["metallicFactor"]    = mat.metallicFactor;
    doc["roughnessFactor"]   = mat.roughnessFactor;
    doc["normalScale"]       = mat.normalScale;
    doc["occlusionStrength"] = mat.occlusionStrength;
    doc["alphaMode"]         = alphaModeName(mat.alphaMode);
    doc["alphaCutoff"]       = mat.alphaCutoff;
    doc["doubleSided"]       = mat.doubleSided;

    json slots = json::object();
    std::string error;

    const bool slotsOk =
        writeSlot(slots, "baseColor",         mat.baseColorTexture,         resources, cache, error) &&
        writeSlot(slots, "metallicRoughness", mat.metallicRoughnessTexture, resources, cache, error) &&
        writeSlot(slots, "normal",            mat.normalTexture,            resources, cache, error) &&
        writeSlot(slots, "occlusion",         mat.occlusionTexture,         resources, cache, error) &&
        writeSlot(slots, "emissive",          mat.emissiveTexture,          resources, cache, error);

    if (!slotsOk) {
        showError("saveMaterial: " + error + ". Saving would lose it, so nothing was written.");
        return false;
    }

    doc["textures"] = std::move(slots);

    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);

    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) {
        showError("saveMaterial: cannot open " + file.string() + " for writing");
        return false;
    }

    out << doc.dump(2) << '\n';
    if (!out) {
        showError("saveMaterial: write failed for " + file.string());
        return false;
    }
    return true;
}

uint32_t loadMaterial(VulkanContext &ctx, ResourceStore &resources, TextureCache &cache,
                      VkCommandBuffer cmd, const std::filesystem::path &file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        showError("loadMaterial: cannot open " + file.string());
        return 0;
    }

    json doc;
    try {
        in >> doc;
    } catch (const json::exception &e) {
        showError("loadMaterial: " + file.string() + " is not valid JSON -- " + e.what());
        return 0;
    }

    int version = 0;
    readIfPresent(doc, "version", version);
    if (version > kFormatVersion) {
        showError("loadMaterial: " + file.string() + " was written by a newer build");
        return 0;
    }

    Material mat;

    // Name falls back to the filename, so a hand-written .mat with no "name"
    // still shows something useful in the Materials grid.
    mat.name = file.stem().string();
    readIfPresent(doc, "name", mat.name);

    readVec4(doc, "baseColorFactor", mat.baseColorFactor);
    readVec3(doc, "emissiveFactor",  mat.emissiveFactor);
    readIfPresent(doc, "emissiveStrength",  mat.emissiveStrength);
    readIfPresent(doc, "metallicFactor",    mat.metallicFactor);
    readIfPresent(doc, "roughnessFactor",   mat.roughnessFactor);
    readIfPresent(doc, "normalScale",       mat.normalScale);
    readIfPresent(doc, "occlusionStrength", mat.occlusionStrength);
    readIfPresent(doc, "alphaCutoff",       mat.alphaCutoff);
    readIfPresent(doc, "doubleSided",       mat.doubleSided);

    std::string alphaMode = "Opaque";
    readIfPresent(doc, "alphaMode", alphaMode);
    mat.alphaMode = alphaModeFromName(alphaMode);

    if (const auto slots = doc.find("textures"); slots != doc.end() && slots->is_object()) {
        mat.baseColorTexture         = readSlot(*slots, "baseColor",         ctx, resources, cache, cmd);
        mat.metallicRoughnessTexture = readSlot(*slots, "metallicRoughness", ctx, resources, cache, cmd);
        mat.normalTexture            = readSlot(*slots, "normal",            ctx, resources, cache, cmd);
        mat.occlusionTexture         = readSlot(*slots, "occlusion",         ctx, resources, cache, cmd);
        mat.emissiveTexture          = readSlot(*slots, "emissive",          ctx, resources, cache, cmd);
    }

    return resources.addMaterial(mat);
}
