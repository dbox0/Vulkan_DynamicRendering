#pragma once
#include <cstdint>
#include <string_view>

class GeometryStore;

// Procedural meshes for the editor's Create menu.
//
//   * right handed, Y up, counter-clockwise winding seen from outside
//   * unit sized and centred on the origin -- except Plane, which is 10x10
//   * UV origin top-left, V down (glTF)
//   * tangent.w == -1 throughout: bitangent = cross(normal, tangent) * w,
//     and V running down the texture puts the bitangent opposite cross().
enum class PrimitiveType : uint8_t
{
    Cube,
    Plane,
    Sphere,
    Cylinder,
    Cone,
    Count
};

const char *primitiveName(PrimitiveType type);

// Inverse of primitiveName(). False for anything that is not a primitive.
bool primitiveFromName(std::string_view name, PrimitiveType &out);

// Allocates out of the geometry store's vertex/index budget and registers the
// mesh. Returns a mesh handle, or 0 if the budget could not take it.
uint32_t buildPrimitive(GeometryStore &geometry, PrimitiveType type, uint32_t materialId = 0);
