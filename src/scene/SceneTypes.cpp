#include "SceneTypes.h"

#include "Geometry/Node.h"
#include "../reflect/Reflection.h"

// WHAT IS -- AND IS NOT -- A NODE FIELD
//
//   Reflected: everything that describes the node and has to survive undo and
//   a save: name, local T/R/S, the light settings.
//
//   Not reflected, deliberately:
//     guid           identity; archives restore data, never who a node is
//     parentId,      slot IDs, only meaningful this frame. The hierarchy is
//     sibling/child  recorded by Guid at the scene level, where sibling
//     links          ORDER is part of the record (undoing a delete has to put
//                    the node back where it was, not at the end)
//     meshId         a runtime handle into GeometryStore. Becomes an asset
//                    reference ("which file, which mesh") with the asset work
//     m_transform,   derived; onFieldsRestored() marks them for rebuild
//     dirty flags
//
// THE LIGHT FIELDS ARE TEMPORARY
//   They move into a LightComponent once component storage exists. Its type
//   will register as "Light" with the same four fields, so the move is a
//   change of owner, not of shape. No scene files exist yet, so there is
//   nothing on disk to migrate either way.
void registerSceneTypes()
{
    reflect::registerEnum<LightType>("LightType")
        .value("None", LightType::None)
        .value("Directional", LightType::Directional);

    reflect::registerType<Node>("Node", 1)
        .field<&Node::name>("name")
        .field<&Node::m_translation>("translation")
        .field<&Node::m_rotation>("rotation").hint(reflect::FieldHint::Rotation)
        .field<&Node::m_scale>("scale")
        .field<&Node::lightType>("lightType")
        .field<&Node::lightColor>("lightColor").hint(reflect::FieldHint::Color)
        .field<&Node::lightIntensity>("lightIntensity").range(0.0f, 20.0f)
        .field<&Node::lightCastsShadows>("lightCastsShadows")
        .afterRead<&Node::onFieldsRestored>();
}
