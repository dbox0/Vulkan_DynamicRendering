#pragma once
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>
#include "../structs.h"
#include "../common/gpu_types.h"

class VulkanContext;

// single global vertex + index buffer, t
// Vertices and indices have NO reserved slot 0 -- vertexStart == 0 is a
// legitimate first submesh. Mesh IDs are 1-based

class GeometryStore
{
public:
    explicit GeometryStore(VulkanContext &ctx) : m_ctx(ctx) {}
    GeometryStore(const GeometryStore &) = delete;
    GeometryStore &operator=(const GeometryStore &) = delete;

    void reserve(size_t vertexBudgetBytes, size_t indexBudgetBytes);
    void shutdown();

    size_t appendVertices(size_t count);
    size_t appendIndices(size_t count);

    Vertex   *vertexAt(size_t index) { return &m_vertices[index]; }
    uint32_t *indexAt(size_t index)  { return &m_indices[index];  }

    uint32_t addMesh(Mesh &&mesh);                          // -> 1-based mesh ID
    const Mesh &mesh(uint32_t meshId) const { return m_meshes[meshId - 1]; }
    size_t meshCount() const { return m_meshes.size(); }

    bool uploadToGpu();

    uint64_t vertexBufferAddress() const { return m_vertexBuffer.deviceAddress; }
    VkBuffer indexBuffer()         const { return m_indexBuffer.vkBuffer; }

private:
    VulkanContext &m_ctx;

    std::vector<Vertex>   m_vertices;
    std::vector<uint32_t> m_indices;
    size_t m_vertOffset = 0;
    size_t m_idxOffset  = 0;
    std::vector<Mesh> m_meshes;

    GPUBuffer m_vertexBuffer;
    GPUBuffer m_indexBuffer;
    bool m_uploaded = false;
};