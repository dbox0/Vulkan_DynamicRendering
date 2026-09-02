#pragma once

struct Vertex {
    glm::vec3 position;
    glm::vec3 color;
    glm::vec3 normal;
    glm::vec2 uv;
};

std::vector<Vertex> m_vertices;
std::vector<uint32_t> m_indices;

struct Image {
    int width;
    int height;
    int channels;
    unsigned char *data;
};

struct Texture {};