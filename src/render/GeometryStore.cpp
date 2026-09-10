#include "GeometryStore.h"

#include <volk.h>
#include <vk_mem_alloc.h>
#include <cassert>
#include <iostream>

#include "VulkanContext.h"
#include "../common/errors.h"

void GeometryStore::reserve(size_t vertexBudgetBytes, size_t indexBudgetBytes)
{
    m_vertices.resize(vertexBudgetBytes / sizeof(Vertex));
    m_indices.resize(indexBudgetBytes / sizeof(uint32_t));
    m_vertOffset = 0;
    m_idxOffset  = 0;
}

void GeometryStore::shutdown()
{
    m_ctx.destroyBuffer(m_vertexBuffer);
    m_ctx.destroyBuffer(m_indexBuffer);
    m_meshes.clear();
    m_vertices.clear();
    m_indices.clear();
    m_vertOffset = 0;
    m_idxOffset  = 0;
    m_uploaded = false;
}

size_t GeometryStore::appendVertices(size_t count)
{
    assert(!m_uploaded && "Cannot append geometry after uploadToGpu()");
    assert(m_vertOffset + count <= m_vertices.size() && "Vertex budget exceeded");

    const size_t start = m_vertOffset;
    m_vertOffset += count;
    return start;
}

size_t GeometryStore::appendIndices(size_t count)
{
    assert(!m_uploaded && "Cannot append geometry after uploadToGpu()");
    assert(m_idxOffset + count <= m_indices.size() && "Index budget exceeded");

    const size_t start = m_idxOffset;
    m_idxOffset += count;
    return start;
}

uint32_t GeometryStore::addMesh(Mesh &&mesh)
{
    m_meshes.push_back(std::move(mesh));
    return static_cast<uint32_t>(m_meshes.size());
}

bool GeometryStore::uploadToGpu()
{
    if (m_uploaded) {
        showError("GeometryStore::uploadToGpu called twice");
        return false;
    }

    // Only the bytes actually written, not the whole budget. The old code
    // staged and copied all 96MB regardless of how much geometry was loaded.
    const size_t vertexBytes = m_vertOffset * sizeof(Vertex);
    const size_t indexBytes  = m_idxOffset  * sizeof(uint32_t);

    if (!vertexBytes || !indexBytes) {
        showError("No geometry to upload");
        return false;
    }

    std::cout << "Uploading geometry: " << m_vertOffset << " verts ("
              << vertexBytes / 1024 / 1024 << " MB), "
              << m_idxOffset << " indices ("
              << indexBytes / 1024 / 1024 << " MB)" << std::endl;

    // --- staging (host visible) ------------------------------------------
    GPUBuffer vertexStage = m_ctx.createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                               vertexBytes, true, VMA_MEMORY_USAGE_AUTO);
    if (!vertexStage.vkBuffer) {
        showError("Error creating vertex staging buffer");
        return false;
    }

    GPUBuffer indexStage = m_ctx.createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                              indexBytes, true, VMA_MEMORY_USAGE_AUTO);
    if (!indexStage.vkBuffer) {
        showError("Error creating index staging buffer");
        m_ctx.destroyBuffer(vertexStage);
        return false;
    }

    m_ctx.mapCopyBufferData(vertexStage, 0, m_vertices.data(), vertexBytes);
    m_ctx.mapCopyBufferData(indexStage,  0, m_indices.data(),  indexBytes);

    // --- device local ----------------------------------------------------
    // SHADER_DEVICE_ADDRESS on the vertex buffer: the vertex shader pulls
    // from it by address rather than through vertex input bindings.

    m_vertexBuffer = m_ctx.createBuffer(
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        vertexBytes, false, VMA_MEMORY_USAGE_AUTO);

    if (!m_vertexBuffer.vkBuffer) {
        showError("Error creating vertex buffer");
        m_ctx.destroyBuffer(vertexStage);
        m_ctx.destroyBuffer(indexStage);
        return false;
    }

    // The index buffer is bound through vkCmdBindIndexBuffer, so it needs a
    // VkBuffer handle rather than an address.
    m_indexBuffer = m_ctx.createBuffer(
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        indexBytes, false, VMA_MEMORY_USAGE_AUTO);

    if (!m_indexBuffer.vkBuffer) {
        showError("Error creating index buffer");
        m_ctx.destroyBuffer(vertexStage);
        m_ctx.destroyBuffer(indexStage);
        m_ctx.destroyBuffer(m_vertexBuffer);
        return false;
    }

    // --- copy ------------------------------------------------------------
    VkCommandBuffer cmd = m_ctx.beginTransient();
    if (!cmd) {
        m_ctx.destroyBuffer(vertexStage);
        m_ctx.destroyBuffer(indexStage);
        return false;
    }

    VkBufferCopy vertexCopy{ .srcOffset = 0, .dstOffset = 0, .size = vertexBytes };
    vkCmdCopyBuffer(cmd, vertexStage.vkBuffer, m_vertexBuffer.vkBuffer, 1, &vertexCopy);

    VkBufferCopy indexCopy{ .srcOffset = 0, .dstOffset = 0, .size = indexBytes };
    vkCmdCopyBuffer(cmd, indexStage.vkBuffer, m_indexBuffer.vkBuffer, 1, &indexCopy);

    m_ctx.endTransient(cmd);

    m_ctx.destroyBuffer(vertexStage);
    m_ctx.destroyBuffer(indexStage);

    m_vertices.clear();
    m_vertices.shrink_to_fit();
    m_indices.clear();
    m_indices.shrink_to_fit();

    m_uploaded = true;
    return true;
}