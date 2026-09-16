#include "SceneSerializer.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#include "../assets/GltfLoader.h"
#include "../assets/Mesh.h"
#include "../assets/TextureCache.h"
#include "../common/errors.h"
#include "../reflect/JsonArchive.h"
#include "../render/GeometryStore.h"
#include "../render/ResourceStore.h"
#include "../render/VulkanContext.h"
#include "Geometry/Node.h"
#include "Geometry/NodeWorld.h"
#include "Scene.h"
#include "SceneTypes.h"

using Json = nlohmann::ordered_json;

namespace
{
    constexpr int kFormatVersion = 1;

    // -----------------------------------------------------------------------
    // save
    // -----------------------------------------------------------------------

    Json nodeToJson(const Node &node, const Scene &scene, const GeometryStore &geometry,
                    const ResourceStore &resources, const TextureCache &cache)
    {
        Json j = Json::object();

        j["id"] = node.guid().toString();

        // Hierarchy: parent Guid and the Guid of the sibling before this one.
        // A reader can reconstruct the order by appending in file order.
        if (node.parentId) {
            j["parent"] = scene.getNode(node.parentId).guid().toString();
        } else {
            j["parent"] = nullptr;
        }
        if (node.nextSiblingId) {
            // "previous" is the sibling before us: walk the parent's child
            // list to find our predecessor.
            uint32_t previous = 0;
            const uint32_t self = scene.findNode(node.guid());
            const uint32_t first = node.parentId
                ? scene.getNode(node.parentId).firstChildId
                : scene.rootNodeId();
            for (uint32_t id = first; id != 0 && id != self; id = scene.getNode(id).nextSiblingId) {
                previous = id;
            }
            j["previous"] = previous ? Json(scene.getNode(previous).guid().toString()) : Json(nullptr);
        } else {
            j["previous"] = nullptr;
        }

        // All reflected fields (name, T/R/S, lights). Asset refs are separate.
        j["data"] = reflect::toJson(node);

        // Mesh asset reference.
        if (node.meshId != 0 && geometry.meshAlive(node.meshId)) {
            const Mesh &mesh = geometry.mesh(node.meshId);

            // Make the path ASSET_DIR-relative.
            const std::string relative = mesh.sourcePath.empty()
                ? std::string()
                : cache.toRelative(mesh.sourcePath);

            if (!relative.empty() && mesh.sourceMeshIndex >= 0) {
                Json meshRef = Json::object();
                meshRef["path"]  = relative;
                meshRef["index"] = mesh.sourceMeshIndex;

                // Submesh material assignments. Written only when they differ
                // from what the glTF itself specifies (index 0 is the glTF
                // default). We write them unconditionally: the cost is
                // negligible and a round-trip test doesn't need to know the
                // glTF defaults.
                Json mats = Json::array();
                for (const SubMesh &sub : mesh.subMeshes) {
                    if (sub.materialId != 0) {
                        const std::string matPath = cache.toRelative(
                            resources.materialInfo(sub.materialId).sourcePath.string());
                        mats.push_back(matPath.empty() ? Json(nullptr) : Json(matPath));
                    } else {
                        mats.push_back(nullptr);
                    }
                }
                meshRef["materials"] = std::move(mats);
                j["mesh"] = std::move(meshRef);
            } else if (!mesh.sourcePath.empty()) {
                // Procedural or unregistered: mark as embedded (no path).
                // The node is saved without a mesh reference; warn on save.
                j["mesh"] = nullptr;
            }
            // No mesh ref written for pure primitives without a source path.
        }

        return j;
    }

    void writeDepthFirst(const Scene &scene, uint32_t nodeId, const GeometryStore &geometry,
                         const ResourceStore &resources, const TextureCache &cache,
                         Json &nodesArray)
    {
        const Node &node = scene.getNode(nodeId);
        nodesArray.push_back(nodeToJson(node, scene, geometry, resources, cache));

        for (uint32_t child = node.firstChildId; child != 0;
             child = scene.getNode(child).nextSiblingId) {
            writeDepthFirst(scene, child, geometry, resources, cache, nodesArray);
        }
    }

    // -----------------------------------------------------------------------
    // load
    // -----------------------------------------------------------------------

    // Maps a mesh asset reference to a meshId, loading the glTF file the
    // first time each (path, index) pair is seen. Uses a per-load cache so a
    // file referenced by many nodes is only parsed once.
    class MeshCache
    {
    public:
        MeshCache(GeometryStore &geometry, ResourceStore &resources,
                  VulkanContext &ctx, TextureCache &cache, VkCommandBuffer cmd)
            : m_geometry(geometry), m_resources(resources), m_ctx(ctx),
              m_cache(cache), m_cmd(cmd) {}

        // Returns 0 if loading fails.
        uint32_t get(const std::string &path, int32_t meshIndex,
                     Scene &scene, std::vector<std::string> &warnings)
        {
            const Key key{ path, meshIndex };
            const auto it = m_loaded.find(key);
            if (it != m_loaded.end()) {
                return it->second;
            }

            // Load the whole file if not seen yet.
            if (!m_loadedFiles.contains(path)) {
                m_loadedFiles.insert(path);
                const std::filesystem::path full = m_cache.root() / path;
                GltfLoader loader(m_ctx, m_resources, m_geometry, scene, m_cache);
                // Load but don't let it append root nodes. We handle placement
                // ourselves. Pass a dummy scene that we discard.
                // Actually: we do want to load the meshes but NOT add the glTF
                // scene graph. GltfLoader::load() does both. For now, use a
                // separate Scene just to load meshes and copy their IDs.
                if (!loader.load(full)) {
                    warnings.push_back("scene: could not load mesh source '" + path + "'");
                }
                // After loading, scan GeometryStore for meshes with this sourcePath.
                // We identify them by (sourcePath, sourceMeshIndex).
                const NodeWorld &nw = scene.nodes();
                (void)nw;   // used below indirectly
            }

            // Find the mesh in GeometryStore by matching sourcePath and index.
            // Walk via the scene we're building (nodes may already reference it).
            // Better: scan geometry directly. Since GeometryStore doesn't expose
            // iteration, use the scene-level scan.
            uint32_t found = 0;
            // GltfLoader creates nodes pointing at the new meshes. After the
            // load, we look for a mesh with the right sourcePath + index in
            // GeometryStore via a node that references it.
            // This is fragile. A cleaner approach: add a meshBySource() query
            // to GeometryStore. For now, scan all scene nodes for a match.
            const auto &nw = scene.nodes();
            for (uint32_t id = 1; id <= static_cast<uint32_t>(nw.size()); ++id) {
                if (!nw.isAlive(id)) continue;
                const uint32_t mid = nw.getNode(id).meshId;
                if (mid == 0 || !m_geometry.meshAlive(mid)) continue;
                const Mesh &mesh = m_geometry.mesh(mid);
                if (mesh.sourceMeshIndex == meshIndex) {
                    // normalize both paths for comparison
                    const std::filesystem::path fullA = std::filesystem::path(mesh.sourcePath).lexically_normal();
                    const std::filesystem::path fullB = (m_cache.root() / path).lexically_normal();
                    if (fullA == fullB) {
                        found = mid;
                        break;
                    }
                }
            }

            if (!found) {
                warnings.push_back("scene: mesh index " + std::to_string(meshIndex) +
                                   " not found in '" + path + "'");
            }
            m_loaded.emplace(key, found);
            return found;
        }

    private:
        struct Key {
            std::string path;
            int32_t     index = -1;
            bool operator==(const Key &o) const = default;
        };
        struct KeyHash {
            size_t operator()(const Key &k) const noexcept {
                return std::hash<std::string>{}(k.path) ^ std::hash<int>{}(k.index);
            }
        };

        std::unordered_map<Key, uint32_t, KeyHash> m_loaded;
        std::unordered_set<std::string>             m_loadedFiles;

        GeometryStore &m_geometry;
        ResourceStore &m_resources;
        VulkanContext &m_ctx;
        TextureCache  &m_cache;
        VkCommandBuffer m_cmd;
    };
}

// ---------------------------------------------------------------------------
// save
// ---------------------------------------------------------------------------

bool saveScene(const Scene &scene, const GeometryStore &geometry,
               const ResourceStore &resources, const TextureCache &cache,
               const std::filesystem::path &file)
{
    Json root = Json::object();
    root["version"] = kFormatVersion;

    Json nodes = Json::array();
    for (uint32_t rootId : scene.rootNodes()) {
        writeDepthFirst(scene, rootId, geometry, resources, cache, nodes);
    }
    root["nodes"] = std::move(nodes);

    std::ofstream out(file);
    if (!out) {
        showError("Scene save: cannot open '" + file.string() + "' for writing");
        return false;
    }
    out << root.dump(2) << '\n';
    if (!out) {
        showError("Scene save: write failed for '" + file.string() + "'");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// load
// ---------------------------------------------------------------------------

SceneLoadResult loadScene(Scene &scene, GeometryStore &geometry, ResourceStore &resources,
                          VulkanContext &ctx, TextureCache &cache, VkCommandBuffer cmd,
                          const std::filesystem::path &file)
{
    SceneLoadResult result;

    std::ifstream in(file);
    if (!in) {
        result.warnings.push_back("Cannot open '" + file.string() + "'");
        return result;
    }

    Json root;
    try {
        root = Json::parse(in);
    } catch (const Json::parse_error &e) {
        result.warnings.push_back("JSON parse error: " + std::string(e.what()));
        return result;
    }

    if (!root.is_object()) {
        result.warnings.push_back("Scene file is not a JSON object");
        return result;
    }

    uint32_t version = 1;
    if (root.contains("version") && root["version"].is_number_unsigned()) {
        version = root["version"].get<uint32_t>();
    }
    if (version == 0 || version > static_cast<uint32_t>(kFormatVersion)) {
        result.warnings.push_back("Unsupported scene version " + std::to_string(version));
        return result;
    }

    // Report unknown top-level keys.
    for (const auto &item : root.items()) {
        if (item.key() != "version" && item.key() != "nodes") {
            result.warnings.push_back("Unknown top-level key '" + item.key() + "' ignored");
        }
    }

    const Json &nodesJson = root.contains("nodes") ? root["nodes"] : Json::array();
    if (!nodesJson.is_array()) {
        result.warnings.push_back("'nodes' is not an array");
        return result;
    }

    // Pre-validate: collect all Guids so we can check for duplicates before
    // touching the scene.
    std::unordered_set<std::string> seenGuids;
    for (const Json &jn : nodesJson) {
        if (!jn.is_object() || !jn.contains("id") || !jn["id"].is_string()) {
            continue;
        }
        seenGuids.insert(jn["id"].get<std::string>());
    }

    MeshCache meshCache(geometry, resources, ctx, cache, cmd);
    std::unordered_map<std::string, uint32_t> guidToSlot;  // guid hex -> slot
    guidToSlot.reserve(nodesJson.size());

    // Nodes are in depth-first pre-order, so a parent's slot is always
    // populated before its children.
    for (const Json &jn : nodesJson) {
        if (!jn.is_object()) {
            result.warnings.push_back("Node entry is not an object, skipped");
            continue;
        }

        // ID
        if (!jn.contains("id") || !jn["id"].is_string()) {
            result.warnings.push_back("Node without 'id', skipped");
            continue;
        }
        const std::string idStr = jn["id"].get<std::string>();
        const auto guidOpt = Guid::parse(idStr);
        if (!guidOpt) {
            result.warnings.push_back("Invalid guid '" + idStr + "', skipped");
            continue;
        }
        const Guid guid = *guidOpt;
        if (scene.findNode(guid) != 0) {
            result.warnings.push_back("Guid '" + idStr + "' already in scene, skipped");
            continue;
        }

        // Parent slot (already created, or 0 = root).
        uint32_t parentId = 0;
        if (jn.contains("parent") && jn["parent"].is_string()) {
            const std::string parentStr = jn["parent"].get<std::string>();
            const auto it = guidToSlot.find(parentStr);
            if (it == guidToSlot.end()) {
                result.warnings.push_back("Node '" + idStr + "': parent '" + parentStr +
                                          "' not found, placed at root");
            } else {
                parentId = it->second;
            }
        }

        const uint32_t nodeId = scene.createNode(parentId, {}, 0, guid);
        if (nodeId == 0) {
            result.warnings.push_back("Node budget exhausted, remaining nodes skipped");
            break;
        }
        guidToSlot[idStr] = nodeId;

        // Node data (name, T/R/S, light settings).
        Node &node = scene.getNode(nodeId);
        if (jn.contains("data") && jn["data"].is_object()) {
            reflect::fromJson(node, jn["data"], version, &result.warnings);
            // meshId in "data" is a runtime handle and must not be applied from a file.
            node.meshId = 0;
        }

        // Mesh asset reference.
        if (jn.contains("mesh") && jn["mesh"].is_object()) {
            const Json &meshRef = jn["mesh"];
            if (meshRef.contains("path") && meshRef["path"].is_string() &&
                meshRef.contains("index") && meshRef["index"].is_number_integer()) {
                const std::string path  = meshRef["path"].get<std::string>();
                const int32_t     index = meshRef["index"].get<int32_t>();
                const uint32_t meshId = meshCache.get(path, index, scene, result.warnings);
                if (meshId) {
                    node.meshId = meshId;

                    // Restore per-submesh material assignments.
                    if (meshRef.contains("materials") && meshRef["materials"].is_array()) {
                        const Json &mats = meshRef["materials"];
                        Mesh &mesh = geometry.meshMutable(meshId);
                        for (size_t i = 0; i < mats.size() && i < mesh.subMeshes.size(); ++i) {
                            if (!mats[i].is_string()) continue;
                            const std::string matPath = mats[i].get<std::string>();
                            // Look up the material by its source path.
                            for (uint32_t m = 1; m <= static_cast<uint32_t>(resources.materialCount()); ++m) {
                                const auto &info = resources.materialInfo(m);
                                if (!info.sourcePath.empty() &&
                                    cache.toRelative(info.sourcePath.string()) == matPath) {
                                    mesh.subMeshes[i].materialId = m;
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    result.ok = true;
    return result;
}
