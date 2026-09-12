#pragma once
#include <string>
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
};