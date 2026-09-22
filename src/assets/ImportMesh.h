#pragma once
#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

struct ImportVertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal  {0.0f};
    glm::vec4 tangent {0.0f};
    glm::vec2 uv      {0.0f};
    glm::vec4 color   {1.0f};
};
static_assert(sizeof(ImportVertex) == 64);

struct ImportMesh
{
    std::vector<ImportVertex> vertices;
    std::vector<uint32_t>     indices;
    bool hasNormals  = false;
    bool hasUVs      = false;
    bool hasTangents = false;
};

struct MeshProcessStats
{
    size_t inputVertices  = 0;
    size_t outputVertices = 0;
};

MeshProcessStats processMesh(ImportMesh &mesh);