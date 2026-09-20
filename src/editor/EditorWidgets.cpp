#include "EditorWidgets.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cfloat>

namespace editor::ui
{
    bool beginProperties(const char *id)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f, 4.0f));
        if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::PopStyleVar();
            return false;
        }
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 76.0f);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
        return true;
    }

    void endProperties()
    {
        ImGui::EndTable();
        ImGui::PopStyleVar();
    }

    void propertyLabel(const char *label)
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
    }
    bool vec3Control(const char *label, glm::vec3 &values,
                     float resetValue, float speed)
    {
        bool changed = false;

        propertyLabel(label);

        ImGui::TableSetColumnIndex(1);
        ImGui::PushID(label);

        const float lineHeight = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f;
        const ImVec2 buttonSize(lineHeight*.2f, lineHeight);

        // Divides the remaining width into three equal fields, accounting for buttons
        ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth() - buttonSize.x * 3.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

        struct Axis { const char *name; ImVec4 base, hover, active; float *value; };
        const Axis axes[3]
        {
            { "X", ImVec4(0.72f, 0.14f, 0.18f, 1.0f), ImVec4(0.82f, 0.32f, 0.36f, 1.0f),
                   ImVec4(0.72f, 0.24f, 0.28f, 1.0f), &values.x },
            { "Y", ImVec4(0.35f, 0.62f, 0.28f, 1.0f), ImVec4(0.43f, 0.72f, 0.36f, 1.0f),
                   ImVec4(0.35f, 0.62f, 0.28f, 1.0f), &values.y },
            { "Z", ImVec4(0.24f, 0.44f, 0.76f, 1.0f), ImVec4(0.32f, 0.52f, 0.86f, 1.0f),
                   ImVec4(0.24f, 0.44f, 0.76f, 1.0f), &values.z }
        };

        for (int i = 0; i < 3; ++i) {
            const Axis &axis = axes[i];

            ImGui::PushStyleColor(ImGuiCol_Button,        axis.base);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, axis.hover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  axis.active);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);

            ImGui::PushID(i);
            if (ImGui::Button(axis.name, buttonSize)) {
                *axis.value = resetValue;
                changed = true;
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(3);

            ImGui::SameLine();
            changed |= ImGui::DragFloat("##v", axis.value, speed, 0.0f, 0.0f, "%.3f");
            ImGui::PopID();
            ImGui::PopItemWidth();

            if (i < 2) {
                ImGui::SameLine();
            }
        }

        ImGui::PopStyleVar();
        ImGui::PopID();
        return changed;
    }
    bool sliderRow(const char *label, float &value, float min, float max,
                   const char *format)
    {
        propertyLabel(label);

        ImGui::TableSetColumnIndex(1);
        ImGui::PushID(label);
        ImGui::SetNextItemWidth(-FLT_MIN);
        const bool changed = ImGui::SliderFloat("##v", &value, min, max, format);
        ImGui::PopID();
        return changed;
    }

    bool sliderIntRow(const char *label, int &value, int min, int max, const char *format){
        propertyLabel(label);

        ImGui::TableSetColumnIndex(1);
        ImGui::PushID(label);
        ImGui::SetNextItemWidth(-FLT_MIN);
        const bool changed = ImGui::SliderInt("##v", &value, min, max, format);
        ImGui::PopID();
        return changed;
    }

    bool dragRow(const char *label, float &value, float speed,
                 float min, float max, const char *format, int flags)
    {
        propertyLabel(label);

        ImGui::TableSetColumnIndex(1);
        ImGui::PushID(label);
        ImGui::SetNextItemWidth(-FLT_MIN);
        const bool changed = ImGui::DragFloat("##v", &value, speed, min, max, format,
                                              static_cast<ImGuiSliderFlags>(flags));
        ImGui::PopID();
        return changed;
    }

    bool comboRow(const char *label, int &index, const char *const *items, int count)
    {
        propertyLabel(label);

        ImGui::TableSetColumnIndex(1);
        ImGui::PushID(label);
        ImGui::SetNextItemWidth(-FLT_MIN);
        const bool changed = ImGui::Combo("##v", &index, items, count);
        ImGui::PopID();
        return changed;
    }

}
