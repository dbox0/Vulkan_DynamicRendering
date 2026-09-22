#include "ImportMesh.h"

#include <meshoptimizer.h>
#include <numeric>

namespace
{
    void unweld(ImportMesh &mesh)
    {
        std::vector<ImportVertex> soup(mesh.indices.size());
        for (size_t i = 0; i < mesh.indices.size(); ++i) {
            soup[i] = mesh.vertices[mesh.indices[i]];
        }
        mesh.vertices = std::move(soup);
        std::iota(mesh.indices.begin(), mesh.indices.end(), 0u);
    }

    void generateFlatNormals(ImportMesh &mesh)
    {
        for (size_t i = 0; i + 2 < mesh.vertices.size(); i += 3) {
            ImportVertex &a = mesh.vertices[i];
            ImportVertex &b = mesh.vertices[i + 1];
            ImportVertex &c = mesh.vertices[i + 2];

            const glm::vec3 n = glm::cross(b.position - a.position, c.position - a.position);
            const float len = glm::length(n);
            a.normal = b.normal = c.normal = len > 1e-20f ? n / len : glm::vec3(0.0f, 1.0f, 0.0f);
        }
    }

    void generateTangents(ImportMesh &mesh)
    {
        std::vector<glm::vec4> tangents(mesh.vertices.size());
        meshopt_generateTangents(&tangents[0].x, nullptr, mesh.vertices.size(),
                                 &mesh.vertices[0].position.x, mesh.vertices.size(), sizeof(ImportVertex),
                                 &mesh.vertices[0].normal.x, sizeof(ImportVertex),
                                 &mesh.vertices[0].uv.x, sizeof(ImportVertex),
                                 meshopt_TangentCompatible);
        for (size_t i = 0; i < mesh.vertices.size(); ++i) {
            tangents[i].w *= -1;
            mesh.vertices[i].tangent = tangents[i];
        }
    }

    void canonicalizeZeros(ImportVertex &v)
    {
        v.position += 0.0f;
        v.normal   += 0.0f;
        v.tangent  += 0.0f;
        v.uv       += 0.0f;
        v.color    += 0.0f;
    }

    void weld(ImportMesh &mesh)
    {
        for (ImportVertex &v : mesh.vertices) {
            canonicalizeZeros(v);
        }

        std::vector<uint32_t> remap(mesh.vertices.size());
        const size_t unique = meshopt_generateVertexRemap(remap.data(), mesh.indices.data(), mesh.indices.size(),
                                                          mesh.vertices.data(), mesh.vertices.size(),
                                                          sizeof(ImportVertex));
        meshopt_remapIndexBuffer(mesh.indices.data(), mesh.indices.data(), mesh.indices.size(), remap.data());
        meshopt_remapVertexBuffer(mesh.vertices.data(), mesh.vertices.data(), mesh.vertices.size(),
                                  sizeof(ImportVertex), remap.data());
        mesh.vertices.resize(unique);
    }

    void optimize(ImportMesh &mesh)
    {
        meshopt_optimizeVertexCache(mesh.indices.data(), mesh.indices.data(), mesh.indices.size(),
                                    mesh.vertices.size());
        const size_t used = meshopt_optimizeVertexFetch(mesh.vertices.data(), mesh.indices.data(), mesh.indices.size(),
                                                        mesh.vertices.data(), mesh.vertices.size(),
                                                        sizeof(ImportVertex));
        mesh.vertices.resize(used);
    }
}
glm::vec3 anyTangent(glm::vec3 n) // Branchless ONB
{
    const float s = std::copysign(1.0f, n.z);
    const float a = -1.0f / (s + n.z);
    return { 1.0f + s * n.x * n.x * a, s * n.x * n.y * a, -s * n.x };
}
void sanitizeFrame(ImportVertex &v)
{
    const float nl = glm::length(v.normal);
    v.normal = (nl > 1e-12f && std::isfinite(nl)) ? v.normal / nl : glm::vec3(0, 1, 0);

    glm::vec3 t = glm::vec3(v.tangent);
    const float tl = glm::length(t);
    t = (tl > 1e-12f && std::isfinite(tl)) ? t / tl : anyTangent(v.normal);
    v.tangent = glm::vec4(t, v.tangent.w < 0.0f ? -1.0f : 1.0f);
}

MeshProcessStats processMesh(ImportMesh &mesh)
{
    MeshProcessStats stats{ .inputVertices = mesh.vertices.size() };

    if (mesh.indices.empty()) {
        mesh.indices.resize(mesh.vertices.size());
        std::iota(mesh.indices.begin(), mesh.indices.end(), 0u);
    }
    mesh.indices.resize(mesh.indices.size() - mesh.indices.size() % 3);

    const bool needNormals  = !mesh.hasNormals;
    const bool needTangents = mesh.hasUVs && (needNormals || !mesh.hasTangents);

    if (needNormals || needTangents) {
        unweld(mesh);
        if (needNormals) {
            generateFlatNormals(mesh);
            mesh.hasNormals = true;
        }
        if (needTangents) {
            generateTangents(mesh);
            mesh.hasTangents = true;
        }
    }
    
    for (ImportVertex &v : mesh.vertices) {
        sanitizeFrame(v);
    }

    if (!mesh.indices.empty()) {
        weld(mesh);
        optimize(mesh);
    } else {
        mesh.vertices.clear();
    }

    stats.outputVertices = mesh.vertices.size();
    return stats;
}