#include "EditorWorld.h"

#include <algorithm>
#include <unordered_set>

#include "../assets/Material.h"
#include "../assets/Mesh.h"
#include "../render/resources/GeometryStore.h"
#include "../render/resources/ResourceStore.h"
#include "../scene/Scene.h"

namespace
{
    bool sameBytes(std::span<const uint8_t> a, std::span<const uint8_t> b)
    {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin());
    }
}

// ---------------------------------------------------------------------------
// materials and mesh assignments

bool EditorWorld::existsOther(const EditTarget &target) const
{
    switch (target.kind) {
    case EditTarget::Kind::Material:
        return target.material != 0 && target.material <= m_resources.materialCount();
    case EditTarget::Kind::MeshMaterials:
        return m_geometry.meshAlive(target.mesh);
    default:
        return false;
    }
}

bool EditorWorld::captureOther(const EditTarget &target, reflect::Blob &out) const
{
    if (!existsOther(target)) {
        return false;
    }
    out.clear();

    switch (target.kind) {
    case EditTarget::Kind::Material:
        reflect::writeBinary(m_resources.material(target.material), out);
        return true;

    case EditTarget::Kind::MeshMaterials:
    {
        MeshMaterials assignment;
        for (const SubMesh &subMesh : m_geometry.mesh(target.mesh).subMeshes) {
            assignment.materials.push_back(subMesh.materialId);
        }
        reflect::writeBinary(assignment, out);
        return true;
    }

    default:
        return false;
    }
}

bool EditorWorld::applyOther(const EditTarget &target, std::span<const uint8_t> snapshot)
{
    if (!existsOther(target)) {
        return false;
    }

    switch (target.kind) {
    case EditTarget::Kind::Material:
    {
        const uint32_t id = target.material;

        // The first write to a material fixes its clean state, if loading or
        // saving has not already.
        if (!m_materialBaseline.contains(id)) {
            reflect::writeBinary(m_resources.material(id), m_materialBaseline[id]);
        }

        // Through a copy: updateMaterial() is what refreshes the GPU copy.
        Material material = m_resources.material(id);
        if (!reflect::readBinary(material, snapshot) || !m_resources.updateMaterial(id, material)) {
            return false;
        }
        m_resources.setMaterialDirty(id, !sameBytes(snapshot, m_materialBaseline[id]));
        return true;
    }

    case EditTarget::Kind::MeshMaterials:
    {
        MeshMaterials assignment;
        if (!reflect::readBinary(assignment, snapshot)) {
            return false;
        }
        Mesh &mesh = m_geometry.meshMutable(target.mesh);
        if (assignment.materials.size() != mesh.subMeshes.size()) {
            return false;
        }
        // Read per frame into the RenderItem buffer: no upload needed.
        for (size_t i = 0; i < mesh.subMeshes.size(); ++i) {
            mesh.subMeshes[i].materialId = assignment.materials[i];
        }
        return true;
    }

    default:
        return false;
    }
}

void EditorWorld::markMaterialSaved(uint32_t materialId)
{
    if (materialId == 0 || materialId > m_resources.materialCount()) {
        return;
    }
    reflect::Blob &baseline = m_materialBaseline[materialId];
    baseline.clear();
    reflect::writeBinary(m_resources.material(materialId), baseline);
    m_resources.setMaterialDirty(materialId, false);
}

// ---------------------------------------------------------------------------
// mesh lifetime

void EditorWorld::onMeshesOrphaned(const std::vector<uint32_t> &meshes)
{
    for (const uint32_t mesh : meshes) {
        if (std::find(m_meshesToRelease.begin(), m_meshesToRelease.end(), mesh) ==
            m_meshesToRelease.end()) {
            m_meshesToRelease.push_back(mesh);
        }
    }
    m_newCandidates = true;
}

void EditorWorld::collectGarbage(const UndoHistory &history)
{
    if (m_meshesToRelease.empty()) {
        return;
    }
    if (!m_newCandidates && history.revision() == m_seenRevision) {
        return;
    }
    m_newCandidates = false;
    m_seenRevision  = history.revision();

    // Every mesh any recorded state points at.
    std::unordered_set<uint32_t> referenced;
    Node scratch;
    const auto fromNode = [&](std::span<const uint8_t> blob) {
        if (!blob.empty() && reflect::readBinary(scratch, blob) && scratch.meshId != 0) {
            referenced.insert(scratch.meshId);
        }
    };

    history.forEachRecord([&](const UndoHistory::Record &record) {
        using Kind = UndoHistory::Record::Kind;
        switch (record.kind) {
        case Kind::Modify:
            if (record.target.kind == EditTarget::Kind::Node) {
                fromNode(record.before);
                fromNode(record.after);
            } else if (record.target.kind == EditTarget::Kind::MeshMaterials) {
                referenced.insert(record.target.mesh);
            }
            break;
        case Kind::Create:
        case Kind::Destroy:
            for (const SubtreeSnapshot::Entry &entry : record.subtree.nodes) {
                fromNode(entry.data);
            }
            break;
        case Kind::Move:
            break;
        }
    });

    std::erase_if(m_meshesToRelease, [&](uint32_t mesh) {
        if (!m_geometry.meshAlive(mesh)) {
            return true;
        }
        // In use again (an undo brought its node back). If that node dies
        // again, the destroy re-queues the mesh.
        if (m_scene.meshUsers(mesh) > 0) {
            return true;
        }
        if (referenced.contains(mesh)) {
            return false;       // the history may still need it
        }
        // removeMesh itself defers the range free past in-flight frames.
        m_geometry.removeMesh(mesh);
        return true;
    });
}
