#pragma once
#include <vulkan/vulkan.h>
#include <deque>
#include <vector>
#include <cstdint>
#include <limits>
#include "../assets/Mesh.h"
#include "GpuShared.h"
#include "RangeAllocator.h"
#include "../common/gpu_types.h"

class VulkanContext;

// One device-local vertex buffer and one index buffer, allocated once at the
// full budget and suballocated with a coalescing free list. Models can be
// loaded and unloaded in any order; freed space is reused.

// The CPU-side mirrorgrows to the mark of what has been allocated,

// OFFSETS: a CPU element index and its offset into the GPU buffer are the
// same number.

// MESH IDS are opaque handles: the low 24 bits are a 1-based
// slot, the top 8 a generation that bumps on every removeMesh. A node holding
// a mesh ID whose slot has since been recycled fails meshAlive(). 0 is never
// a valid handle.
//
// Scene caches DrawItems holding `const SubMesh *` across
// frames, and Renderer holds a pointer to that cache, so a Mesh must not move
// once it has been added. Slots live in a deque

// revision() bumps on every add or remove so the cache knows to rebuild.

// LIFETIME: removeMesh() does not release the ranges immediately -- frames
// already submitted may still be reading them. The ranges go on a pending
// list and come back into circulation from tick(), after the frame that
// removed them

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
    // Returns the start offset, or kInvalidOffset when the budget cannot
    // satisfy the request

    // The returned range is marked dirty, so whatever the caller writes into
    // it gets picked up by the next flushUploads().
    size_t allocateVertices(size_t count);
    size_t allocateIndices(size_t count);

    // Writable views into the CPU mirror. The pointer is invalidated by any
    // subsequent allocate call
    Vertex   *vertexAt(size_t index) { return &m_vertices[index]; }
    uint32_t *indexAt(size_t index)  { return &m_indices[index];  }

    const Vertex   *vertexAt(size_t index) const { return &m_vertices[index]; }
    const uint32_t *indexAt(size_t index)  const { return &m_indices[index];  }

    // For edits to geometry that is already resident

    void touchVertices(size_t firstVertex, size_t count);
    void touchIndices(size_t firstIndex, size_t count);

    // --- meshes ----------------------------------------------------------
    // addMesh takes ownership of the ranges its submeshes point at;
    // removeMesh gives them back
    uint32_t addMesh(Mesh &&mesh);
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

    uint64_t vertexBufferAddress() const { return m_vertexBuffer.deviceAddress; }
    VkBuffer indexBuffer()         const { return m_indexBuffer.vkBuffer; }

    // --- stats for the editor ------------------------------------------
    size_t vertexCapacity() const { return m_vertexAlloc.capacity(); }
    size_t indexCapacity()  const { return m_indexAlloc.capacity(); }
    size_t vertexUsed()     const { return m_vertexAlloc.used(); }
    size_t indexUsed()      const { return m_indexAlloc.used(); }
    size_t vertexResident() const { return m_vertices.size(); }   // CPU mirror size
    size_t indexResident()  const { return m_indices.size(); }
    size_t largestFreeVertexBlock() const { return m_vertexAlloc.largestFreeBlock(); }
    size_t largestFreeIndexBlock()  const { return m_indexAlloc.largestFreeBlock(); }

private:
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

    std::vector<Vertex>   m_vertices;
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