#pragma once

#import <glm/glm.hpp>

struct Plane {
    using Vector3 = glm::vec3;
    glm::vec3 normal;
    float distance; // distance from origin

    Plane() : normal(0.0f, 1.0f, 0.0f), distance(0.0f) {}

    Plane(const Vector3 normal, float d) : normal(n.normalized()), distance(d) {}

    Plane(const Vector3& point, const Vector3& n) {
        normal = n.normalized();
        distance = normal.dot(point); // d = N . P
    }
    Plane(const Vector3& p1, const Vector3& p2, const Vector3& p3) {
        Vector3 v1 = p2 - p1;
        Vector3 v2 = p3 - p1;
        normal = v1.cross(v2).normalized();
        distance = normal.dot(p1);
    }
};