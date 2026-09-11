#pragma once
#include <glm/fwd.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include "../Component.h"

class TransformComponent : public Component {

    glm::vec3 position = glm::vec3(0.0f);
    glm::quat rotation = glm::quat(1.0f,0.0f,0.0f,0.0f);
    glm::vec3 scale = glm::vec3(1.0f);

    mutable glm::mat4 transformMatrix = glm::mat4(1.0f);
    mutable bool transformDirty = true;

public:

    void setPosition(glm::vec3 pos) {
        position = pos;
        transformDirty = true;
    }

    void setRotation(glm::quat& rot) {
        rotation = rot;

    }

    void setScale(const glm::vec3& s) {
        scale = s;
        transformDirty = true;
    }

    void setTransform(glm::mat4 &transform) {
        glm::vec3 skew;
        glm::vec4 perspective;
        glm::decompose(transform, scale, rotation, position, skew, perspective);

        transformMatrix = transform;
        transformDirty = false;
    }

    const glm::vec3& getPosition() const {return position;}
    const glm::quat& getRotation() const {return rotation;}
    const glm::vec3& getScale() const {return scale;}

    glm::mat4 getTransformationMatrix() const {
        if (transformDirty) {
            glm::mat4 transMat = glm::translate(glm::mat4(1.0f),position);
            glm::mat4 rotMat = glm::mat4_cast(rotation);
            glm::mat4 scaleMat = glm::scale(glm::mat4(1.0f),scale);

            transformMatrix = transMat * rotMat * scaleMat;
            transformDirty = false;
        }
        return transformMatrix;
    }

};