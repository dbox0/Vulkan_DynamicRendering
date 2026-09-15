#pragma once
#include <glm/vec3.hpp>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <limits>

#include "../scene/Scene.h"   // DrawItem

class Camera;
class GeometryStore;

// Mouse picking, CPU side.
//
// GeometryStore keeps the vertex and index arrays resident in RAM
// click turns into a world-space ray, the ray is pushed into
// each node's local space, and every triangle the ray's AABB test survives gets
// a Moller-Trumbore test. Nearest hit wins.

// ALTERNATIVE : ID buffer -- render a R32_UINT attachment of node IDs
// and read back the pixel under the cursor. That is exact (picks whatever he shader  drew, alpha cutout included)
// but costs a pass + a readback + a frame of latency. Ray casting costs nothing until the user clicks.


// Ray struct. Will be moved later
struct Ray
{
    glm::vec3 origin{ 0.0f };
    glm::vec3 direction{ 0.0f, 0.0f, -1.0f };   // world space, normalised
};

struct PickResult
{
    uint32_t  nodeId   = 0;                     // 0 = nothing hit
    uint32_t  subMesh  = 0;                     // index into Mesh::subMeshes
    float     distance = std::numeric_limits<float>::max();
    glm::vec3 position{ 0.0f };                 // world-space hit point

    explicit operator bool() const { return nodeId != 0; }
};

// Window pixel -> world ray. mouseX/mouseY are SDL window coordinates
// (origin top-left); width/height must be the same size the camera's aspect
// ratio was built from, or the ray lands off to one side.
Ray screenPointToRay(const Camera &camera, float mouseX, float mouseY,
                     uint32_t width, uint32_t height);

// Nearest triangle hit in the whole scene. `scratch` is the draw-item list the
// traversal fills
// keep alive between calls so a click doesn't allocate.
PickResult pickNode(Scene &scene, const GeometryStore &geometry,
                    const Ray &ray);

// Lights have no geometry, so they are picked as a sphere around their origin
// - > same handle the viewport draws.
// Radius is in world units

PickResult pickLight(const Scene &scene, const Ray &ray, float radius = 0.35f);

// Exposed because gizmos and camera-focus will need them too.
bool raySphere(const Ray &ray, const glm::vec3 &center, float radius, float &tNear);
bool rayAabb(const Ray &ray, const glm::vec3 &boundsMin, const glm::vec3 &boundsMax,
             float &tNear);
bool rayTriangle(const Ray &ray, const glm::vec3 &v0, const glm::vec3 &v1,
                 const glm::vec3 &v2, float &t);