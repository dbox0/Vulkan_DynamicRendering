#include "GeometryStore.h"

#include <volk.h>
#include <vk_mem_alloc.h>
#include <cassert>
#include <iostream>

#include "VulkanContext.h"
#include "../common/errors.h"

bool GeometryStore::reserve(size_t vertexBudgetBytes, size_t indexBudgetBytes)
{
    if (m_vertexBuffer.vkBuffer) {
        showError("GeometryStore::reserve called more than once");
        return false;
    }

    m_vertices.resize(vertexBudgetBytes / sizeof(Vertex));
    m_indices.resize(indexBudgetBytes / sizeof(uint32_t));
    m_vertOffset = 0;
    m_idxOffset  = 0;

    // Alloc at full budget once

    m_vertexBuffer = m_ctx.createBuffer(VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        m_vertices.size() * sizeof(Vertex),false, VMA_MEMORY_USAGE_AUTO);

    if (!m_vertexBuffer.vkBuffer) {
        showError("GeometryStore::reserve : Error creating the vertex buffer");
        return false;
    }

    m_indexBuffer = m_ctx.createBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        m_indices.size() * sizeof(uint32_t),false, VMA_MEMORY_USAGE_AUTO);

    if (!m_indexBuffer.vkBuffer) {
        showError("GeometryStore::reserve : Error creating the index buffer");
        m_ctx.destroyBuffer(m_vertexBuffer);
        return false;
    }

    std::cout << "Geometry budget: " << m_vertices.size() << " verts ("
          << vertexBudgetBytes / 1024 / 1024 << " MB), "
          << m_indices.size() << " indices ("
          << indexBudgetBytes / 1024 / 1024 << " MB)" << std::endl;
    return true;

}

void GeometryStore::shutdown()
{
    m_ctx.destroyBuffer(m_vertexBuffer);
    m_ctx.destroyBuffer(m_indexBuffer);
    m_meshes.clear();
    m_vertices.clear();
    m_vertices.shrink_to_fit();
    m_indices.clear();
    m_indices.shrink_to_fit();
    m_vertOffset = 0;
    m_idxOffset  = 0;
}


// Suballocation
size_t GeometryStore::appendVertices(size_t count)
{
    if (count == 0) {
        return m_vertOffset;
    }
    if (m_vertOffset + count > m_vertices.size()) {
        showError("Vertex budget exhausted");
        return kInvalidOffset;
    }

    const size_t start = m_vertOffset;
    m_vertOffset += count;
    return start;
}

size_t GeometryStore::appendIndices(size_t count)
{
    if (count == 0) {
        return m_idxOffset;
    }
    if (m_idxOffset + count > m_indices.size()) {
        showError("Index budget exhausted");
        return kInvalidOffset;
    }

    const size_t start = m_idxOffset;
    m_idxOffset += count;
    return start;
}


uint32_t GeometryStore::addMesh(Mesh &&mesh)
{
    m_meshes.push_back(std::move(mesh));
    return static_cast<uint32_t>(m_meshes.size());
}

// ======== Uploads to GPU ==========

bool GeometryStore::copyToDevice(const void *src, const GPUBuffer &dst,
                                 size_t dstOffsetBytes, size_t bytes, const char* what)
{
    if (bytes == 0) {
        return true;
    }
    if (!dst.vkBuffer) {
        showError(std::string("Upload before reserve(): ") + what);
        return false;
    }

    // TODO: Persistent staging ring would avoid an allocation per upload.
    // revisit when we stream

    GPUBuffer staging = m_ctx.createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT,bytes,true,VMA_MEMORY_USAGE_AUTO);

    if (!staging.vkBuffer) {
        showError(std::string("Error creating staging buffer for ") + what);
        return false;
    }

    m_ctx.mapCopyBufferData(staging,0,src,bytes);
    VkCommandBuffer cmd = m_ctx.beginTransient();
    if (!cmd) {
        m_ctx.destroyBuffer(staging);
        return false;
    }

    VkBufferCopy copy{ .srcOffset = 0, .dstOffset = dstOffsetBytes, .size = bytes };
    vkCmdCopyBuffer(cmd, staging.vkBuffer, dst.vkBuffer, 1, &copy);

    // TODO: Still a full queue stall. Swap for a fence + deferred destroy when the
    // hitch starts showing up in the editor.
    m_ctx.endTransient(cmd);
    m_ctx.destroyBuffer(staging);
    return true;
}

bool GeometryStore::uploadVertexRange(size_t firstVertex, size_t count)
{
    if (count == 0) {
        return true;
    }
    if (firstVertex + count > m_vertices.size()) {
        showError("uploadVertexRange out of bounds");
        return false;
    }
    return copyToDevice(&m_vertices[firstVertex], m_vertexBuffer,
                        firstVertex * sizeof(Vertex), count * sizeof(Vertex),
                        "vertices");
}

bool GeometryStore::uploadIndexRange(size_t firstIndex, size_t count)
{
    if (count == 0) {
        return true;
    }
    if (firstIndex + count > m_indices.size()) {
        showError("uploadIndexRange out of bounds");
        return false;
    }
    return copyToDevice(&m_indices[firstIndex], m_indexBuffer,
                        firstIndex * sizeof(uint32_t), count * sizeof(uint32_t),
                        "indices");
}

bool GeometryStore::uploadSince(const BatchMark &since)
{
    assert(since.vertexStart <= m_vertOffset && since.indexStart <= m_idxOffset &&
           "BatchMark is from the future -- cursors only move forward");

    const size_t vertexCount = m_vertOffset - since.vertexStart;
    const size_t indexCount  = m_idxOffset  - since.indexStart;

    if (vertexCount == 0 && indexCount == 0) {
        // An empty batch is not an error: a glTF with no drawable primitives
        // still imports its node hierarchy.
        return true;
    }

    std::cout << "Uploading batch: " << vertexCount << " verts @ " << since.vertexStart
              << ", " << indexCount << " indices @ " << since.indexStart << std::endl;

    if (!uploadVertexRange(since.vertexStart, vertexCount)) {
        return false;
    }
    return uploadIndexRange(since.indexStart, indexCount);
}
