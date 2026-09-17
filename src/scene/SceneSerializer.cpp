#include "SceneSerializer.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#include "../assets/GltfLoader.h"
#include "../assets/MaterialSerializer.h"
#include "../assets/Mesh.h"
#include "../assets/PrimitiveBuilder.h"
#include "../assets/TextureCache.h"
#include "../reflect/JsonArchive.h"
#include "../render/resources/GeometryStore.h"
#include "../render/resources/ResourceStore.h"
#include "../render/core/VulkanContext.h"
#include "Geometry/Node.h"
#include "Geometry/NodeWorld.h"
#include "Scene.h"
#include "SceneTypes.h"

using Json = nlohmann::ordered_json;

namespace
{
    constexpr int kFormatVersion = 1;

    std::string nodeLabel(const Node &node)
    {
        return node.name.empty() ? std::string("Node") : "'" + node.name + "'";
    }

    // =======================================================================
    // save
    // =======================================================================

    class SceneWriter
    {
    public:
        SceneWriter(const Scene &scene, const GeometryStore &geometry,
                    const ResourceStore &resources, const TextureCache &cache,
                    std::vector<std::string> &warnings)
            : m_scene(scene), m_geometry(geometry), m_resources(resources),
              m_cache(cache), m_warnings(warnings) {}

        void writeDepthFirst(uint32_t nodeId, Json &nodesArray)
        {
            const Node &node = m_scene.getNode(nodeId);
            nodesArray.push_back(nodeToJson(nodeId, node));

            for (uint32_t child = node.firstChildId; child != 0;
                 child = m_scene.getNode(child).nextSiblingId) {
                writeDepthFirst(child, nodesArray);
            }
        }

    private:
        Json nodeToJson(uint32_t self, const Node &node)
        {
            Json j = Json::object();
            j["id"] = node.guid().toString();

            j["parent"] = node.parentId
                ? Json(m_scene.getNode(node.parentId).guid().toString())
                : Json(nullptr);

            // The sibling before this one. Informational for now: the reader
            // relies on file order.
            uint32_t previous = 0;
            const uint32_t first = node.parentId
                ? m_scene.getNode(node.parentId).firstChildId
                : m_scene.rootNodeId();
            for (uint32_t id = first; id != 0 && id != self; id = m_scene.getNode(id).nextSiblingId) {
                previous = id;
            }
            j["previous"] = previous
                ? Json(m_scene.getNode(previous).guid().toString())
                : Json(nullptr);

            j["data"] = reflect::toJson(node);

            if (node.meshId != 0 && m_geometry.meshAlive(node.meshId)) {
                Json meshRef = meshToJson(node, m_geometry.mesh(node.meshId));
                if (!meshRef.is_null()) {
                    j["mesh"] = std::move(meshRef);
                }
            }
            return j;
        }

        Json meshToJson(const Node &node, const Mesh &mesh)
        {
            Json ref = Json::object();
            const bool isPrimitive = !mesh.primitive.empty();

            if (isPrimitive) {
                ref["primitive"] = mesh.primitive;
            } else {
                const std::string relative = mesh.sourcePath.empty()
                    ? std::string()
                    : m_cache.toRelative(mesh.sourcePath);
                if (relative.empty() || mesh.sourceMeshIndex < 0) {
                    m_warnings.push_back("Node " + nodeLabel(node) + ": mesh '" + mesh.name +
                                         "' is not a primitive and not a file under the asset "
                                         "folder; saved without its mesh");
                    return nullptr;
                }
                ref["path"]  = relative;
                ref["index"] = mesh.sourceMeshIndex;
            }

            // One number per runtime mesh, in first-seen order: nodes sharing
            // a handle share it again after load.
            const auto [it, inserted] = m_instances.try_emplace(
                node.meshId, static_cast<int>(m_instances.size()));
            ref["instance"] = it->second;

            Json mats = Json::array();
            for (const SubMesh &sub : mesh.subMeshes) {
                mats.push_back(materialRef(node, sub.materialId, isPrimitive));
            }
            ref["materials"] = std::move(mats);
            return ref;
        }

        Json materialRef(const Node &node, uint32_t materialId, bool isPrimitive)
        {
            if (materialId == 0 || materialId > m_resources.materialCount()) {
                return nullptr;
            }
            const std::string relative =
                m_cache.toRelative(m_resources.materialInfo(materialId).sourcePath);
            if (!relative.empty()) {
                return relative;
            }

            // No file to point at. For a glTF mesh null means "the file's own
            // material", which is right unless the user reassigned it
            // that case cannot be told apart here.
            // For a primitive :
            // null means the default, so anything else is a known loss.
            if (isPrimitive && materialId != m_resources.defaultMaterialId()) {
                const std::string &name = m_resources.material(materialId).name;
                m_warnings.push_back("Node " + nodeLabel(node) + ": material '" +
                                     (name.empty() ? std::string("unnamed") : name) +
                                     "' has no .mat file; saved as the default material");
            }
            return nullptr;
        }

        const Scene         &m_scene;
        const GeometryStore &m_geometry;
        const ResourceStore &m_resources;
        const TextureCache  &m_cache;
        std::vector<std::string> &m_warnings;

        std::unordered_map<uint32_t, int> m_instances;   // meshId -> instance
    };

    // =======================================================================
    // load
    // =======================================================================

    // One lazily opened upload for .mat textures. GltfLoader opens its own,
    // and the uploader allows only one open at a time, so this must be
    // flushed before every glTF load.
    class UploadScope
    {
    public:
        explicit UploadScope(VulkanContext &ctx) : m_ctx(ctx) {}
        ~UploadScope() { flush(); }

        VkCommandBuffer get()
        {
            if (!m_cmd) {
                m_cmd = m_ctx.beginUpload();
            }
            return m_cmd;
        }

        void flush()
        {
            if (m_cmd) {
                m_ctx.submitUpload();
                m_cmd = nullptr;
            }
        }

    private:
        VulkanContext  &m_ctx;
        VkCommandBuffer m_cmd = nullptr;
    };

    class AssetResolver
    {
    public:
        AssetResolver(Scene &scene, GeometryStore &geometry, ResourceStore &resources,
                      VulkanContext &ctx, TextureCache &cache, SceneLoadResult &result)
            : m_scene(scene), m_geometry(geometry), m_resources(resources), m_ctx(ctx),
              m_cache(cache), m_result(result), m_uploads(ctx)
        {
            // Every material that already has a file, by its relative path.
            for (uint32_t m = 1; m <= static_cast<uint32_t>(m_resources.materialCount()); ++m) {
                const std::string rel = m_cache.toRelative(m_resources.materialInfo(m).sourcePath);
                if (!rel.empty()) {
                    m_materialsByPath.try_emplace(rel, m);
                }
            }
        }

        // Returns 0 when the reference cannot be resolved (warning recorded).
        uint32_t mesh(const Json &ref)
        {
            const bool hasInstance = ref.contains("instance") && ref["instance"].is_number_integer();
            const bool hasPath     = ref.contains("path") && ref["path"].is_string() &&
                                     ref.contains("index") && ref["index"].is_number_integer();
            const bool hasPrim     = ref.contains("primitive") && ref["primitive"].is_string();

            // Sharing key. Files written before "instance" existed share by
            // (path, index) and never share primitives.
            std::string key;
            if (hasInstance) {
                key = "#" + std::to_string(ref["instance"].get<int64_t>());
            } else if (hasPath) {
                key = ref["path"].get<std::string>() + ":" +
                      std::to_string(ref["index"].get<int64_t>());
            }
            if (!key.empty()) {
                if (const auto it = m_byKey.find(key); it != m_byKey.end()) {
                    return it->second;
                }
            }

            uint32_t meshId = 0;
            if (hasPrim) {
                meshId = primitive(ref["primitive"].get<std::string>());
            } else if (hasPath) {
                meshId = fileMesh(ref["path"].get<std::string>(), ref["index"].get<int32_t>());
            } else {
                m_result.warnings.push_back("Mesh reference has neither 'path' nor 'primitive'");
            }

            if (!key.empty()) {
                m_byKey.emplace(key, meshId);
            }
            return meshId;
        }

        // 0 when the path is not a loadable .mat.
        uint32_t material(const std::string &relative)
        {
            if (const auto it = m_materialsByPath.find(relative); it != m_materialsByPath.end()) {
                return it->second;
            }

            const std::filesystem::path full = m_cache.toAbsolute(relative);
            uint32_t materialId = 0;
            if (std::filesystem::exists(full)) {
                if (VkCommandBuffer cmd = m_uploads.get()) {
                    materialId = loadMaterial(m_ctx, m_resources, m_cache, cmd, full);
                }
            }
            if (materialId) {
                m_resources.setMaterialSource(materialId, full);
                m_result.loadedMaterials.push_back(materialId);
            } else {
                m_result.warnings.push_back("Material '" + relative + "' could not be loaded");
            }
            m_materialsByPath.emplace(relative, materialId);   // do not retry
            return materialId;
        }

        // Frees every glTF mesh that was loaded but never handed to a node,
        // and submits pending uploads.
        void finish()
        {
            m_uploads.flush();
            for (auto &[path, copies] : m_files) {
                for (FileCopy &copy : copies) {
                    for (size_t i = 0; i < copy.meshIds.size(); ++i) {
                        if (!copy.used[i] && copy.meshIds[i]) {
                            m_geometry.removeMesh(copy.meshIds[i]);
                        }
                    }
                }
            }
        }

    private:
        uint32_t primitive(const std::string &name)
        {
            PrimitiveType type{};
            if (!primitiveFromName(name, type)) {
                m_result.warnings.push_back("Unknown primitive '" + name + "'");
                return 0;
            }
            const uint32_t meshId = buildPrimitive(m_geometry, type, m_resources.defaultMaterialId());
            if (!meshId) {
                m_result.warnings.push_back("Geometry budget exhausted building a " + name);
            }
            return meshId;
        }

        // Each distinct instance of (path, index) needs its own runtime mesh,
        // so a file is loaded again once every copy of that index is taken.
        uint32_t fileMesh(const std::string &path, int32_t index)
        {
            if (index < 0) {
                m_result.warnings.push_back("Negative mesh index in '" + path + "'");
                return 0;
            }
            std::vector<FileCopy> &copies = m_files[path];
            for (FileCopy &copy : copies) {
                if (static_cast<size_t>(index) < copy.meshIds.size() && !copy.used[index]) {
                    copy.used[index] = true;
                    return copy.meshIds[index];
                }
            }

            // Loading creates no nodes, so nothing in the scene moves.
            m_uploads.flush();
            FileCopy copy;
            GltfLoader loader(m_ctx, m_resources, m_geometry, m_scene, m_cache);
            if (!loader.loadMeshesOnly(m_cache.toAbsolute(path), copy.meshIds)) {
                m_result.warnings.push_back("Could not load mesh source '" + path + "'");
                return 0;
            }
            copy.used.assign(copy.meshIds.size(), false);

            if (static_cast<size_t>(index) >= copy.meshIds.size() || !copy.meshIds[index]) {
                m_result.warnings.push_back("'" + path + "' has no mesh " + std::to_string(index));
                copies.push_back(std::move(copy));
                return 0;
            }
            copy.used[index] = true;
            const uint32_t meshId = copy.meshIds[index];
            copies.push_back(std::move(copy));
            return meshId;
        }

        struct FileCopy
        {
            std::vector<uint32_t> meshIds;
            std::vector<bool>     used;
        };

        Scene           &m_scene;
        GeometryStore   &m_geometry;
        ResourceStore   &m_resources;
        VulkanContext   &m_ctx;
        TextureCache    &m_cache;
        SceneLoadResult &m_result;
        UploadScope      m_uploads;

        std::unordered_map<std::string, uint32_t>              m_byKey;
        std::unordered_map<std::string, std::vector<FileCopy>> m_files;
        std::unordered_map<std::string, uint32_t>              m_materialsByPath;
    };

    void applyMaterials(const Json &mats, Mesh &mesh, AssetResolver &assets)
    {
        for (size_t i = 0; i < mats.size() && i < mesh.subMeshes.size(); ++i) {
            if (!mats[i].is_string()) {
                continue;   // null: keep what the mesh came with
            }
            if (const uint32_t materialId = assets.material(mats[i].get<std::string>())) {
                mesh.subMeshes[i].materialId = materialId;
            }
        }
    }

    bool readSceneFile(const std::filesystem::path &file, Json &root, uint32_t &version,
                       std::vector<std::string> &warnings)
    {
        std::ifstream in(file);
        if (!in) {
            warnings.push_back("Cannot open '" + file.string() + "'");
            return false;
        }
        try {
            root = Json::parse(in);
        } catch (const Json::parse_error &e) {
            warnings.push_back("JSON parse error: " + std::string(e.what()));
            return false;
        }
        if (!root.is_object()) {
            warnings.push_back("Scene file is not a JSON object");
            return false;
        }

        version = 1;
        if (root.contains("version") && root["version"].is_number_unsigned()) {
            version = root["version"].get<uint32_t>();
        }
        if (version == 0 || version > static_cast<uint32_t>(kFormatVersion)) {
            warnings.push_back("Unsupported scene version " + std::to_string(version));
            return false;
        }
        if (root.contains("nodes") && !root["nodes"].is_array()) {
            warnings.push_back("'nodes' is not an array");
            return false;
        }
        for (const auto &item : root.items()) {
            if (item.key() != "version" && item.key() != "nodes") {
                warnings.push_back("Unknown top-level key '" + item.key() + "' ignored");
            }
        }
        return true;
    }
}

// ---------------------------------------------------------------------------
// save
// ---------------------------------------------------------------------------

SceneSaveResult saveScene(const Scene &scene, const GeometryStore &geometry,
                          const ResourceStore &resources, const TextureCache &cache,
                          const std::filesystem::path &file)
{
    SceneSaveResult result;

    Json root = Json::object();
    root["version"] = kFormatVersion;

    Json nodes = Json::array();
    SceneWriter writer(scene, geometry, resources, cache, result.warnings);
    for (const uint32_t rootId : scene.rootNodes()) {
        writer.writeDepthFirst(rootId, nodes);
    }
    root["nodes"] = std::move(nodes);

    std::error_code ec;
    if (file.has_parent_path()) {
        std::filesystem::create_directories(file.parent_path(), ec);
    }

    std::filesystem::path temp = file;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::trunc);
        if (!out) {
            result.warnings.push_back("Cannot open '" + temp.string() + "' for writing");
            return result;
        }
        out << root.dump(2) << '\n';
        out.flush();
        if (!out) {
            result.warnings.push_back("Write failed for '" + temp.string() + "'");
            std::filesystem::remove(temp, ec);
            return result;
        }
    }

    std::filesystem::rename(temp, file, ec);
    if (ec) {
        result.warnings.push_back("Cannot replace '" + file.string() + "': " + ec.message());
        std::filesystem::remove(temp, ec);
        return result;
    }

    result.ok = true;
    return result;
}

// ---------------------------------------------------------------------------
// load
// ---------------------------------------------------------------------------

SceneLoadResult loadScene(Scene &scene, GeometryStore &geometry, ResourceStore &resources,
                          VulkanContext &ctx, TextureCache &cache,
                          const std::filesystem::path &file,
                          const std::function<void()> &clearExisting)
{
    SceneLoadResult result;

    Json     root;
    uint32_t version = 1;
    if (!readSceneFile(file, root, version, result.warnings)) {
        return result;   // current scene untouched
    }

    if (clearExisting) {
        clearExisting();
    }

    const Json nodesJson = root.contains("nodes") ? root["nodes"] : Json::array();

    AssetResolver assets(scene, geometry, resources, ctx, cache, result);
    std::unordered_map<std::string, uint32_t> guidToSlot;   // guid hex -> slot
    guidToSlot.reserve(nodesJson.size());

    // Depth-first pre-order: a parent is always created before its children.
    for (const Json &jn : nodesJson) {
        if (!jn.is_object() || !jn.contains("id") || !jn["id"].is_string()) {
            result.warnings.push_back("Node entry without a valid 'id', skipped");
            continue;
        }
        const std::string idStr = jn["id"].get<std::string>();
        const auto guid = Guid::parse(idStr);
        if (!guid) {
            result.warnings.push_back("Invalid guid '" + idStr + "', skipped");
            continue;
        }
        if (scene.findNode(*guid) != 0) {
            result.warnings.push_back("Guid '" + idStr + "' already in scene, skipped");
            continue;
        }

        uint32_t parentId = 0;
        if (jn.contains("parent") && jn["parent"].is_string()) {
            const std::string parentStr = jn["parent"].get<std::string>();
            if (const auto it = guidToSlot.find(parentStr); it != guidToSlot.end()) {
                parentId = it->second;
            } else {
                result.warnings.push_back("Node '" + idStr + "': parent '" + parentStr +
                                          "' not found, placed at root");
            }
        }

        // Checked up front so a mesh is never built for a node that cannot exist.
        if (scene.nodes().liveCount() >= scene.maxNodes()) {
            result.warnings.push_back("Node budget exhausted, remaining nodes skipped");
            break;
        }

        // Mesh first: it may load files, and no Node& is held across that.
        uint32_t meshId = 0;
        if (jn.contains("mesh") && jn["mesh"].is_object()) {
            const Json &meshRef = jn["mesh"];
            meshId = assets.mesh(meshRef);
            if (meshId && meshRef.contains("materials") && meshRef["materials"].is_array()) {
                applyMaterials(meshRef["materials"], geometry.meshMutable(meshId), assets);
            }
        }

        // meshId goes through createNode so the scene's refcount sees it.
        const uint32_t nodeId = scene.createNode(parentId, {}, meshId, *guid);
        if (nodeId == 0) {
            result.warnings.push_back("Node budget exhausted, remaining nodes skipped");
            break;
        }
        guidToSlot[idStr] = nodeId;

        if (jn.contains("data") && jn["data"].is_object()) {
            Node &node = scene.getNode(nodeId);
            if (!reflect::fromJson(node, jn["data"], version, &result.warnings)) {
                result.warnings.push_back("Node '" + idStr + "': data could not be read");
            }
            // "data" carries meshId as a runtime handle; the file's value is
            // meaningless in this session.
            node.meshId = meshId;
        }
    }

    assets.finish();
    result.ok = true;
    return result;
}
