#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>

#include "../common/Guid.h"

// What an edit is about: one object whose reflected state can be captured as
// a snapshot and written back.
//
// THE KINDS ARE STORAGE KINDS, NOT FEATURES.
//   A kind exists per place objects are kept (NodeWorld, the material table)
//   and knows how to find an object there and push a changed copy back into
//   it. A new component, light type or material field needs no new kind
//   it rides on its storage's kind. Components will add one kind, "component
//   of node X of type T", covering every component type there will ever be.
//
// Every kind names its object by something that survives the object's slot
// being reused: a Guid for nodes; for materials the ID, which is stable only
// because materials are never removed (NOTE: THIS WILL CHANGE)
// for meshes the generation-tagged mesh handle, which never names a different mesh once its
// own has been freed.
struct EditTarget
{
    enum class Kind : uint8_t
    {
        None,
        Node,
        Material,
        MeshMaterials
    };

    Kind     kind     = Kind::None;
    Guid     node;              // Kind::Node
    uint32_t material = 0;      // Kind::Material
    uint32_t mesh     = 0;      // Kind::MeshMaterials

    [[nodiscard]] static EditTarget forNode(Guid guid)
    {
        EditTarget target;
        target.kind = Kind::Node;
        target.node = guid;
        return target;
    }

    [[nodiscard]] static EditTarget forMaterial(uint32_t materialId)
    {
        EditTarget target;
        target.kind     = Kind::Material;
        target.material = materialId;
        return target;
    }

    [[nodiscard]] static EditTarget forMeshMaterials(uint32_t meshId)
    {
        EditTarget target;
        target.kind = Kind::MeshMaterials;
        target.mesh = meshId;
        return target;
    }

    [[nodiscard]] bool isValid() const
    {
        switch (kind) {
        case Kind::Node:          return !node.isNull();
        case Kind::Material:      return material != 0;
        case Kind::MeshMaterials: return mesh != 0;
        case Kind::None:          break;
        }
        return false;
    }

    friend bool operator==(const EditTarget &, const EditTarget &) = default;
};

template <>
struct std::hash<EditTarget>
{
    size_t operator()(const EditTarget &t) const noexcept
    {
        size_t h = static_cast<size_t>(t.kind);
        h = h * 1000003u ^ std::hash<Guid>{}(t.node);
        h = h * 1000003u ^ t.material;
        h = h * 1000003u ^ t.mesh;
        return h;
    }
};
