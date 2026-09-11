#pragma once
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>
#include "../structs.h"
#include "../common/gpu_types.h"

class VulkanContext;



// One global vertex + index buffer -> allocated once at full budget and suballocated
// via bump cursor.

// CPU-side arrays stay resident: they are the authoring surface for
// procedural meshes and the source for raycast/picking.
// The budget is paid in RAM __AND__ VRAM.


// Mesh IDS are 1 based; 0 = none
// Vertex and Index offsets are NOT! -- offset 0 is a legitimate first submesh

class GeometryStore
{
public:
    static constexpr size_t kInvalidOffset = std::numeric_limits<size_t>::max();

    struct BatchMark {
        size_t vertexStart = 0;
        size_t indexStart = 0;
    };

    explicit GeometryStore(VulkanContext &ctx) : m_ctx(ctx) {}
    GeometryStore(const GeometryStore &) = delete;
    GeometryStore &operator=(const GeometryStore &) = delete;

    // Allocated device local buffers at full budget. Must be called after context
    // is set up and before any append.
    // Returns false on alloc failure
    bool reserve(size_t vertexBudgetBytes, size_t indexBudgetBytes);
    void shutdown();


    // Suballocation
    // Returns start offset or kInvalidOffset if budget is exhausted. Callers must check this at runtime
    // (example : Model too big -> recoverable and not a programming mistake)
    size_t appendVertices(size_t count);
    size_t appendIndices(size_t count);

    Vertex   *vertexAt(size_t index) { return &m_vertices[index]; }
    uint32_t *indexAt(size_t index)  { return &m_indices[index];  }

    uint32_t addMesh(Mesh &&mesh);                          // -> 1-based mesh ID
    const Mesh &mesh(uint32_t meshId) const { return m_meshes[meshId - 1]; }
    size_t meshCount() const { return m_meshes.size(); }


    // ========= Uploading data to the GPU ================= //
    // mark() before a load, uploadSince(mark) after.
    // Uploads exactly the verts and indices appended inbetween

    BatchMark mark() const { return BatchMark{m_vertOffset , m_idxOffset}; }
    bool uploadSince(const BatchMark &since);

    bool uploadVertexRange(size_t firstVertex, size_t count);
    bool uploadIndexRange(size_t firstIndex, size_t count);


    uint64_t vertexBufferAddress() const { return m_vertexBuffer.deviceAddress; }
    VkBuffer indexBuffer()         const { return m_indexBuffer.vkBuffer; }


    size_t vertexCapacity() const{ return m_vertices.size(); }
    size_t indexCapacity()  const{ return m_indices.size(); }
    size_t vertexUsed()     const{ return m_vertOffset;}
    size_t indexUsed()      const{ return m_idxOffset;}

private:
    VulkanContext &m_ctx;

    std::vector<Vertex>   m_vertices;
    std::vector<uint32_t> m_indices;
    size_t m_vertOffset = 0;
    size_t m_idxOffset  = 0;
    std::vector<Mesh> m_meshes;

    GPUBuffer m_vertexBuffer;
    GPUBuffer m_indexBuffer;

    bool copyToDevice(const void *src, const GPUBuffer &dst,
                  size_t dstOffsetBytes, size_t bytes, const char *what);

};