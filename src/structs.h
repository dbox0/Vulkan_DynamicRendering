#pragma once
#include <glm/glm.hpp>
#include <vector>

struct SubMesh;
struct Vertex
{
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 color = glm::vec3(1.0f);
    glm::vec3 normal = glm::vec3(0.0f);
    glm::vec2 uv = glm::vec2(0.0f);
};

struct Mesh
{
    std::string name;
    std::vector<SubMesh> subMeshes;
};

struct SubMesh
{
    size_t vertexStart = 0;
    size_t vertexCount = 0;
    size_t indexStart = 0;
    size_t indexCount = 0;
    uint32_t materialId = 0;
};

struct Image
{
    int width;
    int height;
    int channels;
    unsigned char *data;
};
struct GPUImage
{
    VkImage image = nullptr;
    VkImageView imageView = nullptr;
    VmaAllocation allocation = nullptr;
};

struct GPUBuffer
{
    VkBuffer vkBuffer = nullptr;
    uint64_t deviceAddress = 0;
    VmaAllocation allocation = nullptr;
};
struct Material
{
    glm::vec4 baseColor = glm::vec4(1, 1, 1, 1);
    uint32_t textureIndex = 0;
};

struct Texture
{
    uint32_t imageId = 0;
    uint32_t samplerId = 0;
};
