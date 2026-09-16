#include "MutationDetector.h"

#include <unordered_set>

#include "../common/Fatal.h"
#include "../editor/UndoHistory.h"
#include "../reflect/BinaryArchive.h"
#include "../render/GeometryStore.h"
#include "../render/ResourceStore.h"
#include "../scene/Scene.h"

// FNV-1a over arbitrary bytes.
static uint64_t fnv1a64(const void *data, size_t bytes, uint64_t seed = 0xcbf29ce484222325ull)
{
    auto p = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < bytes; ++i) {
        seed = (seed ^ p[i]) * 0x100000001b3ull;
    }
    return seed;
}

static uint64_t mixBlob(const reflect::Blob &blob, uint64_t h)
{
    const uint64_t size = blob.size();
    h = fnv1a64(&size, sizeof(size), h);
    if (!blob.empty()) {
        h = fnv1a64(blob.data(), blob.size(), h);
    }
    return h;
}

bool MutationDetector::isEnabled() const
{
#ifndef NDEBUG
    return m_scene != nullptr;
#else
    return false;
#endif
}

void MutationDetector::initialize(const Scene &scene, const ResourceStore &resources,
                                  const GeometryStore &geometry, const UndoHistory &history)
{
    m_scene     = &scene;
    m_resources = &resources;
    m_geometry  = &geometry;
    m_history   = &history;
}

uint64_t MutationDetector::hashState() const
{
    uint64_t h = 0xcbf29ce484222325ull;

    // Every live node's reflected data, in a deterministic order.
    // Traversal order (slot 1..N, skipping dead) is stable because slots are
    // never shuffled -- only recycled, and a recycled slot's new node has a
    // different reflected state. The hash therefore changes on both death and
    // creation.
    const NodeWorld &nw = m_scene->nodes();
    for (uint32_t id = 1; id <= static_cast<uint32_t>(nw.size()); ++id) {
        if (!nw.isAlive(id)) {
            continue;
        }
        const reflect::Blob blob = reflect::toBlob(nw.getNode(id));
        h = fnv1a64(&id, sizeof(id), h);
        h = mixBlob(blob, h);
    }

    // Every material. Material IDs are append-only and never recycled, so
    // index order is stable and unambiguous.
    for (uint32_t m = 1; m <= static_cast<uint32_t>(m_resources->materialCount()); ++m) {
        const reflect::Blob blob = reflect::toBlob(m_resources->material(m));
        h = fnv1a64(&m, sizeof(m), h);
        h = mixBlob(blob, h);
    }

    // Every live mesh's material assignments. The mesh handle is
    // generation-tagged, so a recycled slot is distinguishable.
    // GeometryStore does not expose an iterator, so we walk mesh IDs 1..N by
    // slot (generation 0 is the low-24-bit slot value cast from 0).
    // However, GeometryStore intentionally hides its internal table. The
    // simplest safe approach: hash the per-submesh materialIds through nodes
    // that reference live meshes. Each unique live meshId appears once because
    // the scene is the only thing that maps them.
    {
        std::unordered_set<uint32_t> seen;
        seen.reserve(64);
        for (uint32_t id = 1; id <= static_cast<uint32_t>(nw.size()); ++id) {
            if (!nw.isAlive(id)) continue;
            const uint32_t meshId = nw.getNode(id).meshId;
            if (meshId == 0 || !m_geometry->meshAlive(meshId) || !seen.insert(meshId).second) {
                continue;
            }
            const Mesh &mesh = m_geometry->mesh(meshId);
            h = fnv1a64(&meshId, sizeof(meshId), h);
            for (const SubMesh &sub : mesh.subMeshes) {
                h = fnv1a64(&sub.materialId, sizeof(sub.materialId), h);
            }
        }
    }

    return h;
}

void MutationDetector::beginFrame()
{
    if (!isEnabled()) {
        return;
    }

    m_wasOpen = m_history->isOpen();
    if (!m_hadState && hashState() != m_hash) {
        fatalError("FATAL ERROR: Untracked mutation detected. Reflected state cha");
    }
}

void MutationDetector::endFrame()
{
    if (!isEnabled()) {
        return;
    }

    m_hash = hashState();
    m_hadState = true;
    // After a transaction closes, recapture for the next frame.
    if (!isOpen) {
        m_hadState = false;
    }
}
