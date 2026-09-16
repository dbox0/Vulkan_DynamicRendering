#pragma once
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/fwd.hpp>
#include <glm/vec3.hpp>
#include <glm/detail/type_quat.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <string>
#include <cstdint>

#include "../../common/Guid.h"

class NodeWorld;
void registerSceneTypes();

// A node carries a light instead of a light being its own object type: the
// transform, the hierarchy, the gizmo, the picker and the inspector all already
// work on nodes

enum class LightType : uint8_t
{
    None,
    Directional
};

// TWO DIRTY FLAGS
//   m_localDirty -- the cached local matrix disagrees with T/R/S.
//                   Set by setTranslation/setRotation/setScale,
//                   cleared by getTransform().
//   m_changed    -- something changed since the last world-transform pass.
//                   Set by every mutator, cleared by NodeWorld. 
class Node
{
    // Identity. Assigned by NodeWorld when the node is created and never
    // changed afterwards -- NodeWorld keeps a Guid -> slot map that must not
    // go stale, which is why nothing else can write it. Not a reflected
    // field either: reading a snapshot into a node restores its data, never
    // its identity.
    Guid m_guid;

    glm::vec3 m_translation = glm::vec3(0, 0, 0);
    glm::vec3 m_scale       = glm::vec3(1.0f);
    glm::quat m_rotation    = glm::quat(1, 0, 0, 0);
    glm::mat4 m_transform   = glm::mat4(1.0f);

    bool m_localDirty = true;
    bool m_changed    = true;

    friend class NodeWorld;
    friend void registerSceneTypes();   // reflects the private transform fields

    // afterRead hook: an archive just wrote T/R/S straight into the fields,
    // bypassing the setters, so redo what the setters would have done.
    void onFieldsRestored()
    {
        m_localDirty = true;
        m_changed    = true;
    }

public:
    // Nodes used to borrow the mesh name in the hierarchy, which left an empty
    // node and a second instance of the same mesh indistinguishable. Editor
    // created nodes need a name of their own regardless.
    std::string name;

    uint32_t meshId        = 0;
    uint32_t parentId      = 0;

    // Direction is NOT stored: it is -Z of the world matrix, glTF's convention
    // for lights. Rotating the node with the gizmo aims the light, and a
    // parented light follows its parent
    LightType lightType          = LightType::None;
    glm::vec3 lightColor         = glm::vec3(1.0f, 0.96f, 0.9f);
    float     lightIntensity     = 3.0f;
    bool      lightCastsShadows  = true;

    uint32_t nextSiblingId = 0;
    uint32_t firstChildId  = 0;

    [[nodiscard]] Guid guid() const { return m_guid; }

    [[nodiscard]] bool hasChanged() const { return m_changed; }
    void clearChanged() { m_changed = false; }

    glm::vec3 getTranslation() const { return m_translation; }
    glm::quat getRotation() const    { return m_rotation; }
    glm::vec3 getScale() const       { return m_scale; }

    void setTranslation(const glm::vec3 &vec)
    {
        m_translation = vec;
        m_localDirty  = true;
        m_changed     = true;
    }

    void setRotation(const glm::quat &rotation)
    {
        m_rotation   = rotation;
        m_localDirty = true;
        m_changed    = true;
    }

    void setScale(const glm::vec3 &scale)
    {
        m_scale      = scale;
        m_localDirty = true;
        m_changed    = true;
    }

    glm::mat4 &getTransform()
    {
        if (m_localDirty) {
            const glm::mat4 matTrans = glm::translate(glm::mat4(1.0f), m_translation);
            const glm::mat4 matRot   = glm::mat4_cast(m_rotation);
            const glm::mat4 matScale = glm::scale(glm::mat4(1.0f), m_scale);

            m_transform  = matTrans * matRot * matScale;
            m_localDirty = false;
        }
        return m_transform;
    }

    // const ref so the gizmo can pass a temporary. Keeps the matrix it was
    // given rather than recomposing from the decomposition, which would only
    // fold decompose()'s rounding error back in on every drag frame.
    void setTransform(const glm::mat4 &transform)
    {
        glm::vec3 skew;
        glm::vec4 perspective;
        glm::decompose(transform, m_scale, m_rotation, m_translation, skew, perspective);

        m_transform  = transform;
        m_localDirty = false;
        m_changed    = true;
    }
};
