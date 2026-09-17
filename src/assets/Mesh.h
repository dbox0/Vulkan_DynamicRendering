#pragma once
#include <string>
#include <vector>
#include <limits>
#include <glm/glm.hpp>

struct SubMesh;

struct MeshMaterials
{
    std::vector<uint32_t> materials;
};

struct Mesh
{
    std::string name;
    std::vector<SubMesh> subMeshes;

    // Asset reference: where this mesh came from, for scene serialization.
    // Path is relative to ASSET_DIR with forward slashes. The index is which
    // of the glTF file's meshes this is (0-based), so a file with several
    // meshes can round-trip without storing vertex data in the scene file.
    // Empty when the mesh was created procedurally (primitives).
    std::string sourcePath;   // e.g. "models/character.gltf"
    int32_t     sourceMeshIndex = -1;

    // Procedural meshes: the generator that built it (primitiveName(), e.g.
    // "Cube"), so a scene can rebuild it instead of storing vertices.
    // Empty for meshes that came from a file.
    std::string primitive;
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