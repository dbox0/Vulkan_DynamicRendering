#include "Picking.h"

#include <algorithm>
#include <cmath>
#include <glm/mat4x4.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include "../render/GeometryStore.h"
#include "../scene/Camera.h"

Ray screenPointToRay(const Camera &camera, float mouseX, float mouseY,
                     uint32_t width, uint32_t height)
{
    Ray ray;
    if (width == 0 || height == 0) {
        return ray;
    }

    const float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
    const glm::mat4 invViewProj = glm::inverse(camera.viewProjection(aspectRatio));


    const float ndcX = 2.0f * (mouseX / static_cast<float>(width)) - 1.0f;
    const float ndcY = 1.0f - 2.0f * (mouseY / static_cast<float>(height));

    // Reverse Z: the near plane is at ndc z = 1, the far plane at 0.
    glm::vec4 nearPoint = invViewProj * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    glm::vec4 farPoint  = invViewProj * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
    nearPoint /= nearPoint.w;
    farPoint  /= farPoint.w;

    ray.origin    = glm::vec3(nearPoint);
    ray.direction = glm::normalize(glm::vec3(farPoint - nearPoint));
    return ray;
}

// Slab test. Written so a zero direction component produces +/-inf rather than a branch:
// comparisons below are false for NaN
// ignoring that axis instead of rejecting a valid hit.
bool rayAabb(const Ray &ray, const glm::vec3 &boundsMin, const glm::vec3 &boundsMax,
             float &tNear)
{
    float t0 = 0.0f;
    float t1 = std::numeric_limits<float>::max();

    for (int axis = 0; axis < 3; ++axis) {
        const float invDir = 1.0f / ray.direction[axis];
        float tSlabNear = (boundsMin[axis] - ray.origin[axis]) * invDir;
        float tSlabFar  = (boundsMax[axis] - ray.origin[axis]) * invDir;
        if (tSlabNear > tSlabFar) {
            std::swap(tSlabNear, tSlabFar);
        }
        t0 = tSlabNear > t0 ? tSlabNear : t0;
        t1 = tSlabFar  < t1 ? tSlabFar  : t1;
        if (t1 < t0) {
            return false;
        }
    }

    tNear = t0;
    return true;
}

// Moller-Trumbore, two-sided
bool rayTriangle(const Ray &ray, const glm::vec3 &v0, const glm::vec3 &v1,
                 const glm::vec3 &v2, float &t)
{
    constexpr float kEpsilon = 1e-8f;

    const glm::vec3 edge1 = v1 - v0;
    const glm::vec3 edge2 = v2 - v0;
    const glm::vec3 pvec  = glm::cross(ray.direction, edge2);
    const float     det   = glm::dot(edge1, pvec);

    if (std::fabs(det) < kEpsilon) {
        return false;                       // ray parallel to the triangle
    }

    const float     invDet = 1.0f / det;
    const glm::vec3 tvec   = ray.origin - v0;

    const float u = glm::dot(tvec, pvec) * invDet;
    if (u < 0.0f || u > 1.0f) {
        return false;
    }

    const glm::vec3 qvec = glm::cross(tvec, edge1);
    const float     v    = glm::dot(ray.direction, qvec) * invDet;
    if (v < 0.0f || u + v > 1.0f) {
        return false;
    }

    const float hit = glm::dot(edge2, qvec) * invDet;
    if (hit < 0.0f) {
        return false;                       // behind the camera
    }

    t = hit;
    return true;
}

PickResult pickNode(Scene &scene, const GeometryStore &geometry,
                    const Ray &ray)
{
    PickResult best;

    for (const DrawItem &item : scene.drawItems(geometry)) {
        const SubMesh &subMesh = *item.subMesh;
        if (subMesh.indexCount < 3 || subMesh.vertexCount == 0) {
            continue;
        }

        // World-space reject first: no inverse, no matrix-vector products.
        float tBounds = 0.0f;
        if (!rayAabb(ray, item.worldBoundsMin, item.worldBoundsMax, tBounds)) {
            continue;
        }
        if (tBounds > best.distance) {
            continue;
        }

        // Survivors only. The direction is deliberately not renormalised, so t
        // stays in world units and stays comparable across submeshes.
        const glm::mat4 invWorld = glm::inverse(item.worldMatrix);
        Ray localRay;
        localRay.origin    = glm::vec3(invWorld * glm::vec4(ray.origin, 1.0f));
        localRay.direction = glm::vec3(invWorld * glm::vec4(ray.direction, 0.0f));

        const Vertex   *vertices = geometry.vertexAt(subMesh.vertexStart);
        const uint32_t *indices  = geometry.indexAt(subMesh.indexStart);

        for (size_t i = 0; i + 2 < subMesh.indexCount; i += 3) {
            // Indices are local to the submesh's vertex block -- the indirect
            // command supplies vertexStart as vertexOffset, so the CPU side has to add it back the same way.
            const glm::vec3 &v0 = vertices[indices[i + 0]].position;
            const glm::vec3 &v1 = vertices[indices[i + 1]].position;
            const glm::vec3 &v2 = vertices[indices[i + 2]].position;

            float t = 0.0f;
            if (rayTriangle(localRay, v0, v1, v2, t) && t < best.distance) {
                best.distance = t;
                best.nodeId   = item.nodeId;
                best.subMesh  = item.subMeshIndex;
                best.position = ray.origin + ray.direction * t;
            }
        }
    }

    return best;
}