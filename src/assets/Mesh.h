#pragma once
#include <string>
#include <vector>
#include <limits>
#include <glm/glm.hpp>

struct SubMesh;

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


    // Local-space AABB, filled by GeometryStore::addMesh. Picking rejects
    // whole submeshes against this before it looks at a single triangle;

    // frustum culling will use this later aswell
    glm::vec3 boundsMin{  std::numeric_limits<float>::max() };
    glm::vec3 boundsMax{ -std::numeric_limits<float>::max() };
};