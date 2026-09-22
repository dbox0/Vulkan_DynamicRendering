#include "GeometryStore.h"

#include <volk.h>
#include <vk_mem_alloc.h>
#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>

#include "../../assets/ImportMesh.h"
#include "../core/VulkanContext.h"
#include "../../common/errors.h"

namespace {
    // Empty Mesh that mesh() can hand back for a stale handle instead of
    // indexing into a recycled slot.
    const Mesh &deadMesh()
    {
        static const Mesh empty{};
        return empty;
    }
}

bool GeometryStore::reserve(size_t vertexBudgetBytes, size_t indexBudgetBytes)
{
    if (m_vertexBuffer.vkBuffer) {
        showError("GeometryStore::reserve called more than once");
        return false;
    }

    const size_t vertexCapacity = vertexBudgetBytes / sizeof(Vertex);
    const size_t indexCapacity  = indexBudgetBytes / sizeof(uint32_t);

    // Only the allocators know the budget. The CPU mirror stays empty until
    // something is actually loaded
    m_vertexAlloc.reset(vertexCapacity);
    m_indexAlloc.reset(indexCapacity);
    m_vertices.clear();
    m_indices.clear();

    m_vertexBuffer = m_ctx.createBuffer(VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        vertexCapacity * sizeof(Vertex), false, VMA_MEMORY_USAGE_AUTO);

    if (!m_vertexBuffer.vkBuffer) {
        showError("GeometryStore::reserve : Error creating the vertex buffer");
        return false;
    }

    m_indexBuffer = m_ctx.createBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        indexCapacity * sizeof(uint32_t), false, VMA_MEMORY_USAGE_AUTO);

    if (!m_indexBuffer.vkBuffer) {
        showError("GeometryStore::reserve : Error creating the index buffer");
        m_ctx.destroyBuffer(m_vertexBuffer);
        return false;
    }

    std::cout << "Geometry budget: " << vertexCapacity << " verts ("
              << vertexBudgetBytes / 1024 / 1024 << " MB VRAM), "
              << indexCapacity << " indices ("
              << indexBudgetBytes / 1024 / 1024 << " MB VRAM), 0 MB resident"
              << std::endl;
    return true;
}

void GeometryStore::shutdown()
{
    m_ctx.destroyBuffer(m_vertexBuffer);
    m_ctx.destroyBuffer(m_indexBuffer);

    m_meshes.clear();
    m_freeSlots.clear();
    m_liveMeshes = 0;
    ++m_revision;

    m_vertices.clear();
    m_vertices.shrink_to_fit();
    m_indices.clear();
    m_indices.shrink_to_fit();

    m_vertexAlloc.reset(0);
    m_indexAlloc.reset(0);
    m_dirtyVertices.clear();
    m_dirtyIndices.clear();
    m_pendingFrees.clear();
}

// ============================================================================
// suballocation
// ============================================================================

void GeometryStore::growVertices(size_t need)
{
    if (need <= m_vertices.size()) {
        return;
    }
    // Geometric growth: resize alone would reallocate exactly, turning a load
    // of many small primitives into a quadratic copy.
    if (need > m_vertices.capacity()) {
        m_vertices.reserve(std::max(need, m_vertices.capacity() * 2));
    }
    m_vertices.resize(need);
}

void GeometryStore::growIndices(size_t need)
{
    if (need <= m_indices.size()) {
        return;
    }
    if (need > m_indices.capacity()) {
        m_indices.reserve(std::max(need, m_indices.capacity() * 2));
    }
    m_indices.resize(need);
}

size_t GeometryStore::allocateVertices(size_t count)
{
    if (count == 0) {
        return 0;
    }
    const size_t offset = m_vertexAlloc.allocate(count);
    if (offset == kInvalidOffset) {
        showError("Vertex budget exhausted (largest free block: " +
                  std::to_string(m_vertexAlloc.largestFreeBlock()) + " verts)");
        return kInvalidOffset;
    }
    growVertices(offset + count);
    m_dirtyVertices.push_back(DirtyRange{ offset, count });
    return offset;
}

size_t GeometryStore::allocateIndices(size_t count)
{
    if (count == 0) {
        return 0;
    }
    const size_t offset = m_indexAlloc.allocate(count);
    if (offset == kInvalidOffset) {
        showError("Index budget exhausted (largest free block: " +
                  std::to_string(m_indexAlloc.largestFreeBlock()) + " indices)");
        return kInvalidOffset;
    }
    growIndices(offset + count);
    m_dirtyIndices.push_back(DirtyRange{ offset, count });
    return offset;
}

void GeometryStore::touchVertices(size_t firstVertex, size_t count)
{
    if (count && firstVertex + count <= m_vertices.size()) {
        m_dirtyVertices.push_back(DirtyRange{ firstVertex, count });
    }
}

void GeometryStore::touchIndices(size_t firstIndex, size_t count)
{
    if (count && firstIndex + count <= m_indices.size()) {
        m_dirtyIndices.push_back(DirtyRange{ firstIndex, count });
    }
}

// ============================================================================
// meshes
// ============================================================================

bool GeometryStore::addSubMesh(const ImportMesh &src, SubMesh &out) {
    if (src.vertices.empty() || src.indices.empty()) {
        return false;
    }
    const size_t vertexStart = allocateVertices(src.vertices.size());
    if (vertexStart == kInvalidOffset) {
        return false;
    }
    const size_t indexStart = allocateIndices(src.indices.size());
    if (indexStart == kInvalidOffset) {
        m_vertexAlloc.release(vertexStart, src.vertices.size());
        return false;
    }

    Vertex *dst = vertexAt(vertexStart);
    for (size_t i = 0; i < src.vertices.size(); ++i) {
        const ImportVertex &v = src.vertices[i];
        dst[i] = Vertex{ .position = v.position, .normal = v.normal, .tangent = v.tangent,
                         .uv = v.uv, .color = v.color };
    }
    std::copy(src.indices.begin(), src.indices.end(), indexAt(indexStart));

    out.vertexStart = vertexStart;
    out.vertexCount = src.vertices.size();
    out.indexStart  = indexStart;
    out.indexCount  = src.indices.size();
    return true;
}

uint32_t GeometryStore::makeHandle(size_t slot, uint8_t generation)
{
    // 24 bits of slot is 16.7M meshes; the generation only has to outlive the
    // stale references in one editing session.
    return (static_cast<uint32_t>(generation) << 24) |
           (static_cast<uint32_t>(slot + 1) & 0x00FFFFFFu);
}

void GeometryStore::computeBounds(SubMesh &subMesh) const
{
    if (subMesh.vertexCount == 0 || subMesh.vertexStart == kInvalidOffset) {
        return;
    }

    glm::vec3 lo( std::numeric_limits<float>::max());
    glm::vec3 hi(-std::numeric_limits<float>::max());

    for (size_t i = 0; i < subMesh.vertexCount; ++i) {
        const glm::vec3 &position = m_vertices[subMesh.vertexStart + i].position;
        lo = glm::min(lo, position);
        hi = glm::max(hi, position);
    }

    subMesh.boundsMin = lo;
    subMesh.boundsMax = hi;
}

uint32_t GeometryStore::addMesh(Mesh &&mesh)
{
    for (SubMesh &subMesh : mesh.subMeshes) {
        computeBounds(subMesh);
    }

    size_t slot = 0;
    if (!m_freeSlots.empty()) {
        slot = m_freeSlots.back();
        m_freeSlots.pop_back();
        m_meshes[slot].mesh  = std::move(mesh);
        m_meshes[slot].alive = true;
    } else {
        slot = m_meshes.size();
        m_meshes.push_back(MeshSlot{ std::move(mesh), 0, true });
    }

    ++m_liveMeshes;
    ++m_revision;
    return makeHandle(slot, m_meshes[slot].generation);
}

bool GeometryStore::meshAlive(uint32_t meshId) const
{
    if (meshId == 0) {
        return false;
    }
    const size_t slot = handleSlot(meshId);
    return slot < m_meshes.size() &&
           m_meshes[slot].alive &&
           m_meshes[slot].generation == handleGeneration(meshId);
}

const Mesh &GeometryStore::mesh(uint32_t meshId) const
{
    if (!meshAlive(meshId)) {
        return deadMesh();
    }
    return m_meshes[handleSlot(meshId)].mesh;
}

Mesh &GeometryStore::meshMutable(uint32_t meshId)
{
    assert(meshAlive(meshId) && "meshMutable on a dead handle");
    return m_meshes[handleSlot(meshId)].mesh;
}

bool GeometryStore::removeMesh(uint32_t meshId)
{
    if (!meshAlive(meshId)) {
        return false;
    }
    const size_t slot = handleSlot(meshId);
    MeshSlot &entry = m_meshes[slot];

    // Deferred: a frame submitted before this call may still be reading these
    // ranges. tick() puts them back once that frame has retired.
    for (const SubMesh &subMesh : entry.mesh.subMeshes) {
        if (subMesh.vertexCount && subMesh.vertexStart != kInvalidOffset) {
            m_pendingFrees.push_back(PendingFree{ subMesh.vertexStart, subMesh.vertexCount, m_frameIndex, false });
        }
        if (subMesh.indexCount && subMesh.indexStart != kInvalidOffset) {
            m_pendingFrees.push_back(PendingFree{ subMesh.indexStart, subMesh.indexCount, m_frameIndex, true });
        }
    }

    entry.mesh = Mesh{};
    entry.alive = false;
    ++entry.generation;          // wrapping is fine; it only has to differ
    m_freeSlots.push_back(slot);
    --m_liveMeshes;
    ++m_revision;

    // A dirty range covering geometry removed before the next flush is left
    // alone on purpose: the space is not reusable until tick() releases it,
    // so the worst case is one wasted copy, never a write into somebody
    // else's range.
    return true;
}

void GeometryStore::tick(uint64_t frameIndex)
{
    m_frameIndex = frameIndex;

    size_t writeIdx = 0;
    for (size_t i = 0; i < m_pendingFrees.size(); ++i) {
        const PendingFree &pending = m_pendingFrees[i];
        if (frameIndex >= pending.removedFrame + kRetireDelay) {
            if (pending.isIndex) {
                m_indexAlloc.release(pending.offset, pending.count);
            } else {
                m_vertexAlloc.release(pending.offset, pending.count);
            }
        } else {
            m_pendingFrees[writeIdx++] = pending;
        }
    }
    m_pendingFrees.resize(writeIdx);
}

// ============================================================================
// uploads
// ============================================================================

void GeometryStore::mergeRanges(std::vector<DirtyRange> &ranges)
{
    if (ranges.size() < 2) {
        return;
    }
    std::sort(ranges.begin(), ranges.end(),
              [](const DirtyRange &a, const DirtyRange &b) { return a.offset < b.offset; });

    size_t writeIdx = 0;
    for (size_t i = 1; i < ranges.size(); ++i) {
        DirtyRange &open = ranges[writeIdx];
        const DirtyRange &next = ranges[i];
        if (next.offset <= open.offset + open.count) {
            open.count = std::max(open.count, next.offset + next.count - open.offset);
        } else {
            ranges[++writeIdx] = next;
        }
    }
    ranges.resize(writeIdx + 1);
}

bool GeometryStore::uploadRanges(VkCommandBuffer cmd, const std::vector<DirtyRange> &ranges,
                                 const void *cpuBase, size_t elementSize,
                                 const GPUBuffer &dst, const char *what)
{
    if (ranges.empty()) {
        return true;
    }
    if (!dst.vkBuffer) {
        showError(std::string("Upload before reserve(): ") + what);
        return false;
    }

    size_t totalBytes = 0;
    for (const DirtyRange &range : ranges) {
        totalBytes += range.count * elementSize;
    }

    // One staging buffer for the whole batch, one copy command with a region
    // per range. The old code made a buffer and a submit per range.
    GPUBuffer staging = m_ctx.createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, totalBytes,
                                           true, VMA_MEMORY_USAGE_AUTO_PREFER_HOST);
    if (!staging.vkBuffer) {
        showError(std::string("Error creating staging buffer for ") + what);
        return false;
    }

    std::vector<VkBufferCopy> copies;
    copies.reserve(ranges.size());

    size_t cursor = 0;
    for (const DirtyRange &range : ranges) {
        const size_t bytes = range.count * elementSize;
        const char *src = static_cast<const char *>(cpuBase) + range.offset * elementSize;
        m_ctx.mapCopyBufferData(staging, cursor, src, bytes);
        copies.push_back(VkBufferCopy{ .srcOffset = cursor,
                                       .dstOffset = range.offset * elementSize,
                                       .size = bytes });
        cursor += bytes;
    }

    vkCmdCopyBuffer(cmd, staging.vkBuffer, dst.vkBuffer,
                    static_cast<uint32_t>(copies.size()), copies.data());

    // The uploader destroys it once the copy has actually run.
    m_ctx.uploader().trackBuffer(cmd, staging);
    return true;
}

bool GeometryStore::flushUploads()
{
    mergeRanges(m_dirtyVertices);
    mergeRanges(m_dirtyIndices);

    if (m_dirtyVertices.empty() && m_dirtyIndices.empty()) {
        //a glTF with no drawable primitives still imports its
        // node hierarchy.
        return true;
    }

    VkCommandBuffer cmd = m_ctx.uploader().begin();
    if (!cmd) {
        return false;
    }

    bool ok = uploadRanges(cmd, m_dirtyVertices, m_vertices.data(), sizeof(Vertex),
                           m_vertexBuffer, "vertices");
    ok = uploadRanges(cmd, m_dirtyIndices, m_indices.data(), sizeof(uint32_t),
                      m_indexBuffer, "indices") && ok;

    // Makes the copies visible to index fetch and to the vertex-pulling loads
    // in every frame submitted after this one on the same queue.
    const VkMemoryBarrier2 barrier
    {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
        .dstAccessMask = VK_ACCESS_2_INDEX_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT
    };
    const VkDependencyInfo dependency
    {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = 1,
        .pMemoryBarriers = &barrier
    };
    vkCmdPipelineBarrier2(cmd, &dependency);

    std::cout << "Uploading geometry: " << m_dirtyVertices.size() << " vertex range(s), "
              << m_dirtyIndices.size() << " index range(s)" << std::endl;

    m_dirtyVertices.clear();
    m_dirtyIndices.clear();

    m_lastUploadTicket = m_ctx.uploader().submit();
    return ok && m_lastUploadTicket != 0;
}