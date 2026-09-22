#include "PrimitiveBuilder.h"

#include "Mesh.h"
#include "../render/resources/GeometryStore.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include "ImportMesh.h"
namespace
{
    constexpr float pi = 3.14159265358979323846f;
    constexpr uint32_t kSegments = 32;
    constexpr uint32_t kRings    = 16;


    ImportVertex makeVertex(const glm::vec3 &position, const glm::vec3 &normal, const glm::vec2 &uv)
    {
        ImportVertex v;
        v.position = position;
        v.normal   = normal;
        v.uv       = uv;
        v.color    = glm::vec4(1.0f);
        return v;
    }

    void pushQuad(ImportMesh &out, const glm::vec3 &u, const glm::vec3 &v, const glm::vec3 &normal,
              float extent = 0.5f)
    {
        const auto base = static_cast<uint32_t>(out.vertices.size());
        const glm::vec3 centre = normal * extent;

        // V flipped, because glTF UVs run downward from the top-left.
        out.vertices.push_back(makeVertex(centre + (-u - v) * extent, normal, { 0.0f, 1.0f }));
        out.vertices.push_back(makeVertex(centre + ( u - v) * extent, normal, { 1.0f, 1.0f }));
        out.vertices.push_back(makeVertex(centre + ( u + v) * extent, normal, { 1.0f, 0.0f }));
        out.vertices.push_back(makeVertex(centre + (-u + v) * extent, normal, { 0.0f, 0.0f }));

        for (const uint32_t i : { 0u, 1u, 2u, 0u, 2u, 3u }) {
            out.indices.push_back(base + i);
        }
    }

    ImportMesh buildCube()
    {
        ImportMesh out;
        out.vertices.reserve(24);
        out.indices.reserve(36);

        pushQuad(out, {  0,  0, -1 }, { 0, 1,  0 }, {  1,  0,  0 });   // +X
        pushQuad(out, {  0,  0,  1 }, { 0, 1,  0 }, { -1,  0,  0 });   // -X
        pushQuad(out, {  1,  0,  0 }, { 0, 0, -1 }, {  0,  1,  0 });   // +Y
        pushQuad(out, {  1,  0,  0 }, { 0, 0,  1 }, {  0, -1,  0 });   // -Y
        pushQuad(out, {  1,  0,  0 }, { 0, 1,  0 }, {  0,  0,  1 });   // +Z
        pushQuad(out, { -1,  0,  0 }, { 0, 1,  0 }, {  0,  0, -1 });   // -Z

        return out;
    }

    // Not Unit Sized

    constexpr float kPlaneSize = 10.0f;

    ImportMesh buildPlane()
    {
        ImportMesh out;
        const auto base = static_cast<uint32_t>(out.vertices.size());

        const glm::vec3 n{ 0, 1, 0 };
        const glm::vec3 u{ 1, 0, 0 };
        const glm::vec3 v{ 0, 0, -1 };     // cross(u, v) == +Y

        const float e = kPlaneSize * 0.5f;

        out.vertices.push_back(makeVertex((-u - v) * e, n, { 0.0f, 1.0f }));
        out.vertices.push_back(makeVertex(( u - v) * e, n, { 1.0f, 1.0f }));
        out.vertices.push_back(makeVertex(( u + v) * e, n, { 1.0f, 0.0f }));
        out.vertices.push_back(makeVertex((-u + v) * e, n, { 0.0f, 0.0f }));

        const uint32_t quad[6] = { 0, 1, 2, 0, 2, 3 };
        for (const uint32_t i : quad) {
            out.indices.push_back(base + i);
        }
        return out;
    }

    ImportMesh buildSphere()
    {
        ImportMesh out;
        out.vertices.reserve((kRings + 1) * (kSegments + 1));
        out.indices.reserve(kRings * kSegments * 6);

        for (uint32_t r = 0; r <= kRings; ++r) {
            const float theta    = pi * static_cast<float>(r) / static_cast<float>(kRings);
            const float sinTheta = std::sin(theta);
            const float cosTheta = std::cos(theta);

            for (uint32_t s = 0; s <= kSegments; ++s) {
                const float phi    = 2.0f * pi * static_cast<float>(s) / static_cast<float>(kSegments);
                const float sinPhi = std::sin(phi);
                const float cosPhi = std::cos(phi);

                const glm::vec3 normal{ sinTheta * sinPhi, cosTheta, sinTheta * cosPhi };
                const glm::vec3 tangent{ cosPhi, 0.0f, -sinPhi };    // d/dphi, already unit
                const glm::vec2 uv{ static_cast<float>(s) / static_cast<float>(kSegments),
                                    static_cast<float>(r) / static_cast<float>(kRings) };

                out.vertices.push_back(makeVertex(normal * 0.5f, normal, uv));
            }
        }

        const uint32_t stride = kSegments + 1;
        for (uint32_t r = 0; r < kRings; ++r) {
            for (uint32_t s = 0; s < kSegments; ++s) {
                const uint32_t top    = r * stride + s;
                const uint32_t bottom = top + stride;

                out.indices.push_back(top);
                out.indices.push_back(bottom);
                out.indices.push_back(top + 1);

                out.indices.push_back(bottom);
                out.indices.push_back(bottom + 1);
                out.indices.push_back(top + 1);
            }
        }
        return out;
    }

    void pushCap(ImportMesh &out, float y, const glm::vec3 &normal)
    {
        const auto centre = static_cast<uint32_t>(out.vertices.size());
        const bool up = normal.y > 0.0f;

        out.vertices.push_back(makeVertex({ 0.0f, y, 0.0f }, normal, { 0.5f, 0.5f }));

        for (uint32_t s = 0; s <= kSegments; ++s) {
            const float phi    = 2.0f * pi * static_cast<float>(s) / static_cast<float>(kSegments);
            const float sinPhi = std::sin(phi);
            const float cosPhi = std::cos(phi);

            out.vertices.push_back(makeVertex({ sinPhi * 0.5f, y, cosPhi * 0.5f }, normal,
                                              { sinPhi * 0.5f + 0.5f, cosPhi * 0.5f + 0.5f }));
        }

        for (uint32_t s = 0; s < kSegments; ++s) {
            const uint32_t a = centre + 1 + s;
            const uint32_t b = centre + 2 + s;

            out.indices.push_back(centre);
            out.indices.push_back(up ? a : b);
            out.indices.push_back(up ? b : a);
        }
    }

    ImportMesh buildCylinder()
    {
        ImportMesh out;
        const auto base = static_cast<uint32_t>(out.vertices.size());
        for (uint32_t s = 0; s <= kSegments; ++s) {
            const float phi    = 2.0f * pi * static_cast<float>(s) / static_cast<float>(kSegments);
            const float sinPhi = std::sin(phi);
            const float cosPhi = std::cos(phi);

            const glm::vec3 normal{ sinPhi, 0.0f, cosPhi };
            const glm::vec3 tangent{ cosPhi, 0.0f, -sinPhi };
            const float u = static_cast<float>(s) / static_cast<float>(kSegments);

            out.vertices.push_back(makeVertex({ normal.x * 0.5f,  0.5f, normal.z * 0.5f },
                                              normal, { u, 0.0f }));
            out.vertices.push_back(makeVertex({ normal.x * 0.5f, -0.5f, normal.z * 0.5f },
                                              normal, { u, 1.0f }));
        }

        for (uint32_t s = 0; s < kSegments; ++s) {
            const uint32_t top    = base + s * 2;
            const uint32_t bottom = top + 1;

            out.indices.push_back(top);
            out.indices.push_back(bottom);
            out.indices.push_back(top + 2);

            out.indices.push_back(bottom);
            out.indices.push_back(bottom + 2);
            out.indices.push_back(top + 2);
        }

        pushCap(out,  0.5f, {  0,  1,  0 });
        pushCap(out, -0.5f, {  0, -1,  0 });
        return out;
    }

    ImportMesh buildCone()
    {
        ImportMesh out;
        const float radius = 0.5f;
        const float height = 1.0f;
        const float slantY = radius / std::sqrt(radius * radius + height * height);
        const float slantR = height / std::sqrt(radius * radius + height * height);

        for (uint32_t s = 0; s < kSegments; ++s) {
            const float phi0 = 2.0f * pi * static_cast<float>(s) / static_cast<float>(kSegments);
            const float phi1 = 2.0f * pi * static_cast<float>(s + 1) / static_cast<float>(kSegments);
            const float phiM = (phi0 + phi1) * 0.5f;

            auto sideVertex = [&](float phi, bool apex)
            {
                const float sinPhi = std::sin(phi);
                const float cosPhi = std::cos(phi);

                const glm::vec3 normal{ sinPhi * slantR, slantY, cosPhi * slantR };
                const glm::vec3 tangent{ cosPhi, 0.0f, -sinPhi };
                const float u = phi / (2.0f * pi);

                const glm::vec3 position = apex
                    ? glm::vec3{ 0.0f, 0.5f, 0.0f }
                    : glm::vec3{ sinPhi * radius, -0.5f, cosPhi * radius };

                return makeVertex(position, normal, { u, apex ? 0.0f : 1.0f });
            };

            const auto base = static_cast<uint32_t>(out.vertices.size());
            out.vertices.push_back(sideVertex(phiM, true));    // apex
            out.vertices.push_back(sideVertex(phi0, false));
            out.vertices.push_back(sideVertex(phi1, false));

            out.indices.push_back(base);
            out.indices.push_back(base + 1);
            out.indices.push_back(base + 2);
        }

        pushCap(out, -0.5f, { 0, -1, 0 });
        return out;
    }
}

const char *primitiveName(PrimitiveType type)
{
    switch (type) {
        case PrimitiveType::Cube:     return "Cube";
        case PrimitiveType::Plane:    return "Plane";
        case PrimitiveType::Sphere:   return "Sphere";
        case PrimitiveType::Cylinder: return "Cylinder";
        case PrimitiveType::Cone:     return "Cone";
        default:                      return "Primitive";
    }
}

bool primitiveFromName(std::string_view name, PrimitiveType &out)
{
    for (uint8_t i = 0; i < static_cast<uint8_t>(PrimitiveType::Count); ++i) {
        const auto type = static_cast<PrimitiveType>(i);
        if (name == primitiveName(type)) {
            out = type;
            return true;
        }
    }
    return false;
}

uint32_t buildPrimitive(GeometryStore &geometry, PrimitiveType type, uint32_t materialId)
{
    ImportMesh data;
    switch (type) {
        case PrimitiveType::Cube:     data = buildCube();     break;
        case PrimitiveType::Plane:    data = buildPlane();    break;
        case PrimitiveType::Sphere:   data = buildSphere();   break;
        case PrimitiveType::Cylinder: data = buildCylinder(); break;
        case PrimitiveType::Cone:     data = buildCone();     break;
        default:                      return 0;
    }

    data.hasNormals = true;
    data.hasUVs     = true;
    processMesh(data);

    SubMesh subMesh;
    subMesh.materialId = materialId;
    if (!geometry.addSubMesh(data, subMesh)) {
        return 0;
    }

    // Indices are local to the submesh the indirect draw supplies
    // vertexOffset = vertexStart
    Mesh mesh;
    mesh.name      = primitiveName(type);
    mesh.primitive = primitiveName(type);   // the scene file's asset reference
    mesh.subMeshes.push_back(subMesh);

    return geometry.addMesh(std::move(mesh));
}
