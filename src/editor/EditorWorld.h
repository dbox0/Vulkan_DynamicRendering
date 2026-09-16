#pragma once
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

#include "SceneUndoWorld.h"

class ResourceStore;
class GeometryStore;

// MESH LIFETIME
//   Deleting a node no longer frees the meshes it leaves unreferenced -- an
//   undo would bring back a node pointing at nothing. Orphaned meshes are
//   queued, and collectGarbage() frees one only when no live node uses it and
//   no transaction in either history stack mentions it.
//
// MATERIAL "UNSAVED" FLAG
//   A material is dirty when it differs from how it was when loaded, saved or
//   first touched -- by content, not by "was it ever written to". Editing a
//   value and putting it back, or undoing to the saved state, clears the
//   asterisk again.
class EditorWorld final : public SceneUndoWorld
{
public:
    EditorWorld(Scene &scene, ResourceStore &resources, GeometryStore &geometry)
        : SceneUndoWorld(scene), m_resources(resources), m_geometry(geometry) {}

    // The material's current state is now its clean state.
    void markMaterialSaved(uint32_t materialId);

    // Cheap when nothing changed since the last call; called once per frame.
    void collectGarbage(const UndoHistory &history);

    [[nodiscard]] size_t pendingMeshCount() const { return m_meshesToRelease.size(); }

    // Call when loading a new scene discards everything the queue held.
    void resetOrphanedMeshes() { m_meshesToRelease.clear(); m_seenRevision = ~uint64_t{0}; }

protected:
    [[nodiscard]] bool existsOther(const EditTarget &target) const override;
    [[nodiscard]] bool captureOther(const EditTarget &target, reflect::Blob &out) const override;
    [[nodiscard]] bool applyOther(const EditTarget &target, std::span<const uint8_t> snapshot) override;
    void onMeshesOrphaned(const std::vector<uint32_t> &meshes) override;

private:
    ResourceStore &m_resources;
    GeometryStore &m_geometry;

    std::unordered_map<uint32_t, reflect::Blob> m_materialBaseline;

    std::vector<uint32_t> m_meshesToRelease;
    bool                  m_newCandidates = false;
    uint64_t              m_seenRevision  = ~uint64_t{ 0 };
};
