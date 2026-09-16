#include "SceneTypes.h"

#include "Geometry/Node.h"
#include "../reflect/Reflection.h"

// WHAT IS -- AND IS NOT -- A NODE FIELD
//
//   Reflected: everything that describes the node and has to survive undo and
//   a save: name, local T/R/S, the light settings.
//
//   Reflected as a RuntimeHandle (in undo snapshots, never in files):
//     meshId         undoing a delete has to bring the mesh link back, but the
//                    number means nothing in another session. It becomes an
//                    asset reference ("which file, which mesh") with the asset
//                    work; until then a scene file cannot say what a node draws.
//
//   Not reflected:
//     guid           identity; archives restore data
//
//     parentId,      slot IDs, only meaningful this frame. The hierarchy is
//     sibling/child  recorded by Guid at the scene level, where sibling
//     links          ORDER is part of the record (undoing a delete has to put
//                    the node back where it was, not at the end)
//
//     m_transform,   derived; onFieldsRestored() marks them for rebuild
//     dirty flags

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
        .field<&Node::meshId>("meshId", reflect::FieldFlags::RuntimeHandle | reflect::FieldFlags::ReadOnly)
        .field<&Node::lightType>("lightType")
        .field<&Node::lightColor>("lightColor").hint(reflect::FieldHint::Color)
        .field<&Node::lightIntensity>("lightIntensity").range(0.0f, 20.0f)
        .field<&Node::lightCastsShadows>("lightCastsShadows")
        .afterRead<&Node::onFieldsRestored>();
}
