#pragma once
#include <vulkan/vulkan.h>
#include <deque>
#include <vector>
#include <cstdint>
#include <limits>
#include "../../assets/Mesh.h"
#include "../GpuShared.h"
#include "RangeAllocator.h"
#include "../core/gpu_types.h"

class VulkanContext;
struct ImportMesh;

class GeometryStore
{
public:
    static constexpr size_t kInvalidOffset = RangeAllocator::kInvalid;

    // Frames a removed range has to survive before its space is reusable.
    // Renderer::MaxFramesInFlight plus one frame of slack.
    static constexpr uint64_t kRetireDelay = 3;

    explicit GeometryStore(VulkanContext &ctx) : m_ctx(ctx) {}
    GeometryStore(const GeometryStore &) = delete;
    GeometryStore &operator=(const GeometryStore &) = delete;

    // Allocates the device-local buffers at full budget. Call after the
    // context is up and before any allocation. Returns false on alloc failure.
    bool reserve(size_t vertexBudgetBytes, size_t indexBudgetBytes);
    void shutdown();

    // --- suballocation ---------------------------------------------------
    // Returns the start offset, or kInvalidOffset
    // The returned range is marked dirty, so whatever the caller writes into
    // it gets picked up by the next flushUploads().
    size_t allocateVertices(size_t count);
    size_t allocateIndices(size_t count);

    // Writable views into the CPU mirror. The pointer is invalidated by any
    // subsequent allocate call
    static constexpr size_t kVertexStride = sizeof(glm::vec3) + sizeof(PackedAttributes) + sizeof(uint32_t);
    static_assert(kVertexStride == 32);

    // For edits to geometry that is already resident
    void touchVertices(size_t firstVertex, size_t count);
    void touchIndices(size_t firstIndex, size_t count);

    // --- meshes ----------------------------------------------------------
    // addMesh takes ownership of the ranges its submeshes point at;
    // removeMesh gives them back
    uint32_t addMesh(Mesh &&mesh);
    bool addSubMesh(const ImportMesh &src, SubMesh &out);

    bool     removeMesh(uint32_t meshId);

    bool        meshAlive(uint32_t meshId) const;
    const Mesh &mesh(uint32_t meshId) const;

    // Mutable access for the editor's material assignment. Does NOT bump
    // revision(): materialId is read per frame into the RenderItem buffer and
    // touches neither the vertex/index ranges nor any cached SubMesh pointer,
    // so forcing a DrawItem rebuild would be pure waste.
    Mesh       &meshMutable(uint32_t meshId);
    size_t      meshCount() const { return m_liveMeshes; }

    // Changes whenever a mesh is added or removed. Anything caching submesh
    // pointers compares this

    uint64_t    revision() const { return m_revision; }

    // --- uploads ---------------------------------------------------------
    // Uploads every dirty range in a single submit and returns without
    // waiting for it. Safe to draw from immediately: the copy is ordered
    // ahead of any frame submitted afterwards on the same queue.
    bool flushUploads();

    // Releases ranges freed by removeMesh once their frame has retired.
    // Call once per frame, after the renderer's frame wait
    void tick(uint64_t frameIndex);

    uint64_t lastUploadTicket() const { return m_lastUploadTicket; }

    const glm::vec3 *positionAt(size_t index) const { return &m_positions[index]; }
    uint64_t positionBufferAddress()  const { return m_positionBuffer.deviceAddress; }
    uint64_t attributeBufferAddress() const { return m_attributeBuffer.deviceAddress; }
    uint64_t colorBufferAddress()     const { return m_colorBuffer.deviceAddress; }
    size_t   vertexResident() const { return m_positions.size(); }

    VkBuffer indexBuffer()         const { return m_indexBuffer.vkBuffer; }
    const uint32_t *indexAt(size_t index) const { return &m_indices[index]; }

    // --- stats for the editor ------------------------------------------
    size_t vertexCapacity() const { return m_vertexAlloc.capacity(); }
    size_t indexCapacity()  const { return m_indexAlloc.capacity(); }
    size_t vertexUsed()     const { return m_vertexAlloc.used(); }
    size_t indexUsed()      const { return m_indexAlloc.used(); }

    size_t indexResident()  const { return m_indices.size(); }
    size_t largestFreeVertexBlock() const { return m_vertexAlloc.largestFreeBlock(); }
    size_t largestFreeIndexBlock()  const { return m_indexAlloc.largestFreeBlock(); }

private:
    template <class T> static void growTo(std::vector<T> &v, size_t need);
    void destroyBuffers();

    struct DirtyRange { size_t offset = 0; size_t count = 0; };

    struct MeshSlot
    {
        Mesh    mesh;
        uint8_t generation = 0;
        bool    alive      = false;
    };

    struct PendingFree
    {
        size_t   offset       = 0;
        size_t   count        = 0;
        uint64_t removedFrame = 0;
        bool     isIndex      = false;
    };

    static uint32_t makeHandle(size_t slot, uint8_t generation);
    static size_t   handleSlot(uint32_t meshId)       { return (meshId & 0x00FFFFFFu) - 1; }
    static uint8_t  handleGeneration(uint32_t meshId) { return static_cast<uint8_t>(meshId >> 24); }

    void growVertices(size_t need);
    void growIndices(size_t need);
    static void mergeRanges(std::vector<DirtyRange> &ranges);
    bool uploadRanges(VkCommandBuffer cmd, const std::vector<DirtyRange> &ranges,
                      const void *cpuBase, size_t elementSize,
                      const GPUBuffer &dst, const char *what);

    // Vertices are already written by the time addMesh is called, so the AABB
    // is swept here instead of burdening every loader with it.
    void computeBounds(SubMesh &subMesh) const;

    VulkanContext &m_ctx;

    std::vector<glm::vec3>        m_positions;
    std::vector<PackedAttributes> m_attributes;
    std::vector<uint32_t>         m_colors;

    GPUBuffer m_positionBuffer;
    GPUBuffer m_attributeBuffer;
    GPUBuffer m_colorBuffer;


    std::vector<uint32_t> m_indices;
    RangeAllocator        m_vertexAlloc;
    RangeAllocator        m_indexAlloc;

    std::deque<MeshSlot> m_meshes;       // deque: references must stay valid
    std::vector<size_t>  m_freeSlots;
    size_t               m_liveMeshes = 0;
    uint64_t             m_revision   = 0;

    std::vector<DirtyRange>  m_dirtyVertices;
    std::vector<DirtyRange>  m_dirtyIndices;
    std::vector<PendingFree> m_pendingFrees;

    uint64_t m_frameIndex        = 0;
    uint64_t m_lastUploadTicket  = 0;

    GPUBuffer m_vertexBuffer;
    GPUBuffer m_indexBuffer;
};