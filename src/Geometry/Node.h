#pragma once
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/fwd.hpp>
#include <glm/vec3.hpp>
#include <glm/detail/type_quat.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>

class Node
{
    glm::vec3 m_translation = glm::vec3(0,0,0);
    glm::vec3 m_scale = glm::vec3(1.0f);
    glm::quat m_rotation = glm::quat(1,0,0,0);
    glm::mat4 m_transform = glm::mat4(1.0f);
    bool m_dirty = true;

public:
    uint32_t mehsId         = 0;
    uint32_t parentId       = 0;
    uint32_t nextSibling    = 0;
    uint32_t firstChildId   = 0;

    glm::vec3 getTranslation() const { return m_translation;}
    glm::quat getRotation() const { return m_rotation;}

    void setRotation(const glm::quat& rotation) {
        this->m_rotation = rotation;
        m_dirty = true;
    }
    glm::vec3 getScale() const { return m_scale;}

    void setScale(const glm::vec3& scale) {
        this->m_scale = scale;
        m_dirty = true;
    }

    glm::mat4 getTransform() {
        if (m_dirty) {
            glm::mat4 matTrans = glm::translate(glm::mat4(1.0f), m_translation);
            glm::mat4 matRot = glm::mat4_cast(m_rotation);
            glm::mat4 matScale = glm::scale(glm::mat4(1.0f), m_scale);
            m_transform = matTrans * matRot * matScale;
            m_dirty = false;
        }
        return m_transform;
    }

    void setTransform(glm::mat4 &transform) {
        glm::vec3 skew;
        glm::vec4 perspective;
         glm::decompose(transform,m_scale,m_rotation,m_translation,skew,perspective);

        m_transform = transform;
        m_dirty = false;
    }
};

