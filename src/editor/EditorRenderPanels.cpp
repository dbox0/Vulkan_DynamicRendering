#include "EditorUI.h"
#include "EditorWidgets.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

#include "../render/passes/ShadowMap.h"
#include "../render/passes/TonemapPass.h"
#include "../render/Renderer.h"

using namespace editor::ui;

namespace
{
    constexpr float MinExposure = 0.01f;
    constexpr float MaxExposure  = 64.0f;
    constexpr float MaxIntensity = 4.0f;

    const char *const TonemapperNames[] = { "None", "Reinhard", "ACES" };
}

void EditorUI::drawShadowWindow()
{
    if (!m_showShadowWindow) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(340.0f, 0.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Shadows", &m_showShadowWindow)) {
        if (!m_shadowSettings) {
            ImGui::TextUnformatted("No renderer bound.");
        } else {
            ShadowSettings &s = *m_shadowSettings;

            ImGui::Checkbox("Enabled", &s.enabled);
            ImGui::Separator();

            beginProperties("shadow_fit");
            sliderRow("Distance", s.distance, 2.0f, 200.0f, "%.1f");
            if (m_sunDirection) {
                // Renormalised by the renderer every frame, so dragging a
                // component to zero is safe.
                vec3Control("Sun dir", *m_sunDirection, 0.0f, 0.01f);
            }
            endProperties();

            ImGui::SeparatorText("Bias");
            beginProperties("shadow_bias");
            sliderRow("Normal",   s.normalBias,   0.0f, 4.0f, "%.2f texels");
            sliderRow("Depth",    s.depthBias,    0.0f, 0.01f, "%.5f");
            sliderRow("Constant", s.constantBias, 0.0f, 8.0f, "%.2f");
            sliderRow("Slope",    s.slopeBias,    0.0f, 8.0f, "%.2f");
            endProperties();

            ImGui::Spacing();
            ImGui::TextDisabled("Raise Slope first if acne appears.");
            ImGui::TextDisabled("Sun dir is the fallback -- a Directional\n"
                                "Light node overrides it.");
        }
    }
    ImGui::End();
}

void EditorUI::drawPostProcessWindow()
{
    if (!m_showPostProcessWindow) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(340.0f, 0.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Post Process", &m_showPostProcessWindow)) {
        if (!m_tonemapSettings) {
            ImGui::TextUnformatted("No renderer bound.");
        } else {
            TonemapConstants &t = *m_tonemapSettings;

            ImGui::SeparatorText("Exposure");

            beginProperties("tonemap_exposure");
            dragRow("Exposure", t.exposure, 0.01f, MinExposure, MaxExposure, "%.3f",
                    ImGuiSliderFlags_Logarithmic);
            endProperties();

            ImGui::TextDisabled("%+.2f EV", std::log2(std::max(t.exposure, MinExposure)));
            ImGui::SameLine();
            if (ImGui::SmallButton("Reset")) {
                t.exposure = 1.0f;
            }

            ImGui::SeparatorText("Tonemapper");

            int curve = static_cast<int>(t.tonemapper);
            beginProperties("tonemap_curve");
            if (comboRow("Curve", curve, TonemapperNames, IM_ARRAYSIZE(TonemapperNames))) {
                t.tonemapper = static_cast<Tonemapper>(curve);
            }
            endProperties();

            ImGui::Spacing();
        }
    }
    ImGui::End();
}

void EditorUI::drawEnvironmentWindow()
{
    if (!m_showEnvironmentWindow) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(340.0f, 0.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Environment", &m_showEnvironmentWindow)) {
        if (!m_environmentSettings) {
            ImGui::TextUnformatted("No renderer bound.");
        } else {
            EnvironmentSettings &e = *m_environmentSettings;

            beginProperties("environment_intensity");
            sliderRow("Environment", e.envIntensity, 0.0f, MaxIntensity, "%.2f");
            sliderRow("Ambient", e.ambientIntensity, 0.0f, MaxIntensity, "%.2f");
            endProperties();

            ImGui::Spacing();
        }
    }
    ImGui::End();
}
