#pragma once

#include <glm/vec3.hpp>

namespace editor::ui
{
    void beginProperties(const char *id);
    void endProperties();

    void propertyLabel(const char *label);

    bool vec3Control(const char *label, glm::vec3 &values,
                     float resetValue = 0.0f, float speed = 0.05f);

    bool sliderRow(const char *label, float &value, float min, float max,
                   const char *format = "%.3f");


    bool sliderIntRow(const char *label, int &value, int min, int max, const char *format = "%d");

    bool dragRow(const char *label, float &value, float speed,
                 float min, float max, const char *format = "%.3f", int flags = 0);

    bool comboRow(const char *label, int &index, const char *const *items, int count);

    
}
