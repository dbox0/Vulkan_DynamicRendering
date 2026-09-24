#include "EditorUI.h"
#include "EditorWidgets.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

#include "../render/shadows/ShadowMap.h"
#include "../render/post/TonemapPass.h"
#include "../render/passes/SkyboxPass.h"
#include "../render/Renderer.h"
#include "../render/post/BloomPass.h"
#include "../render/passes/ao/GtaoPass.h"

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

            ImGui::SeparatorText("Cascades");
            beginProperties("shadow_cascades");
            sliderIntRow("Count",    s.cascadeCount, 1, static_cast<int>(MaxShadowCascades));
            sliderRow("Lambda",      s.splitLambda,  0.0f, 1.0f, "%.2f");
            sliderRow("Log base",    s.splitBase,    0.1f, 5.0f, "%.2f m");
            endProperties();
            ImGui::Checkbox("Debug cascades", &s.debugCascades);

            ImGui::SeparatorText("Bias");
            beginProperties("shadow_bias");
            sliderRow("Normal",   s.normalBias,   0.0f, 4.0f, "%.2f texels");
            sliderRow("Depth",    s.depthBias,    0.0f, 0.2f, "%.3f m");
            sliderRow("Constant", s.constantBias, 0.0f, 8.0f, "%.2f");
            sliderRow("Slope",    s.slopeBias,    0.0f, 8.0f, "%.2f");
            endProperties();

            ImGui::Spacing();
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

        if (m_bloomSettings) {
            ImGui::SeparatorText("Bloom");
            BloomSettings &b = *m_bloomSettings;

            ImGui::Checkbox("Enable bloom", &b.enabled);

            ImGui::BeginDisabled(!b.enabled);
            beginProperties("bloom_settings");
            if (m_tonemapSettings) {
                sliderRow("Strength", m_tonemapSettings->bloomStrength, 0.0f, 1.0f, "%.3f");
            }
            // Scene-referred, so it is not affected by Exposure above.
            dragRow("Threshold", b.threshold, 0.01f, 0.0f, 20.0f, "%.2f");
            sliderRow("Soft knee", b.softKnee, 0.0f, 1.0f, "%.2f");
            dragRow("Radius", b.filterRadius, 0.0002f, 0.0005f, 0.02f, "%.4f");
            endProperties();
            ImGui::EndDisabled();

            if (m_tonemapSettings && m_bloomMipCount > 0) {
                const int maxMip = static_cast<int>(m_bloomMipCount) - 1;
                int mip = std::min<int>(m_tonemapSettings->bloomDebugMip, maxMip);

                beginProperties("bloom_debug");
                sliderIntRow("Debug", mip, -1, maxMip, mip < 0 ? "Composite" : "Mip %d");
                endProperties();

                m_tonemapSettings->bloomDebugMip = mip;
            }
            ImGui::Spacing();
        }

        if (m_gtaoSettings) {
            ImGui::SeparatorText("Ambient occlusion");
            GtaoSettings &g = *m_gtaoSettings;

            ImGui::Checkbox("Enable GTAO", &g.enabled);

            ImGui::BeginDisabled(!g.enabled);
            ImGui::Checkbox("Temporal accumulation", &g.temporal);
            beginProperties("gtao_settings");
            static const char *QualityNames[] = { "Low", "Medium", "High", "Ultra" };
            comboRow("Quality", g.quality, QualityNames, IM_ARRAYSIZE(QualityNames));
            sliderIntRow("Denoise passes", g.denoisePasses, 0, 3);
            dragRow("Radius", g.radius, 0.01f, 0.01f, 10.0f, "%.2f");
            sliderRow("Falloff", g.falloffRange, 0.05f, 1.0f, "%.2f");
            sliderRow("Power", g.finalPower, 0.5f, 4.0f, "%.2f");
            endProperties();
            ImGui::EndDisabled();
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

        if (m_skyboxSettings) {
            ImGui::SeparatorText("Skybox");
            SkyboxSettings &s = *m_skyboxSettings;

            ImGui::Checkbox("Draw skybox", &s.enabled);

            beginProperties("skybox_settings");
            sliderRow("Intensity", s.intensity, 0.0f, MaxIntensity, "%.2f");
            // Reuses the prefiltered chain
            sliderRow("Blur (LOD)", s.lod, 0.0f, 10.0f, "%.1f");
            endProperties();

            ImGui::Spacing();
        }
    }
    ImGui::End();
}

void EditorUI::drawCullWindow()
{
    if (!m_showCullWindow) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(340.0f, 0.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Culling", &m_showCullWindow)) {
        if (!m_cullSettings || !m_cullStats) {
            ImGui::TextUnformatted("No renderer bound.");
            ImGui::End();
            return;
        }

        CullSettings &s = *m_cullSettings;
        const CullStats &stats = *m_cullStats;

        ImGui::Checkbox("Enabled", &s.enabled);
        if (!s.enabled) {
            ImGui::SameLine();
            ImGui::TextDisabled("(everything submitted)");
        }

        ImGui::SeparatorText("Stats");
        {
            const uint32_t culled = stats.total - stats.submitted;
            const float ratio = stats.total ? float(culled) / float(stats.total) : 0.0f;

            ImGui::Text("Submitted %u / %u", stats.submitted, stats.total);
            ImGui::ProgressBar(ratio, ImVec2(-FLT_MIN, 0.0f));
            ImGui::TextDisabled("%u culled (%.0f%%)", culled, ratio * 100.0f);
            //TODO: FIX THIS
           // ImGui::Text("draw lists ms: %.2f",m_drawListMs);
            if (stats.clamped) {
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f),
                                   "Draw limit reached -- list truncated");
            }
        }

        ImGui::SeparatorText("Frozen frustum");

        const bool wasFrozen = s.freeze;
        ImGui::Checkbox("Freeze", &s.freeze);
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::BeginItemTooltip()) {
            ImGui::TextUnformatted("Latches the culling planes. Fly away and\n"
                                   "look back to see what actually survived.");
            ImGui::EndTooltip();
        }

        if (s.freeze && !wasFrozen) {
            m_cullWasFrozen = true;   // seeding happens in build(), which has the Camera
        }

        ImGui::BeginDisabled(!s.freeze);
        ImGui::Checkbox("Override projection", &s.overrideProjection);

        ImGui::BeginDisabled(!s.overrideProjection);
        beginProperties("cull_proj");
        sliderRow("FOV",  s.fovDegrees, 10.0f, 120.0f, "%.1f deg");
        sliderRow("Near", s.nearClip,   0.01f,  10.0f, "%.3f");
        sliderRow("Far",  s.farClip,    1.0f,  500.0f, "%.1f");
        endProperties();
        ImGui::EndDisabled();

        if (s.nearClip >= s.farClip) {
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f), "Near must be < far");
        }
        ImGui::EndDisabled();

        ImGui::SeparatorText("Visualization");
        ImGui::Checkbox("Frustum wireframe", &s.drawFrustum);
        if (s.drawFrustum && !s.freeze) {
            ImGui::SameLine();
            ImGui::TextDisabled("(frozen only)");
        }
        ImGui::Checkbox("Visible bounds", &s.drawVisible);
        ImGui::Checkbox("Culled bounds",  &s.drawCulled);

        if (s.drawVisible || s.drawCulled) {
            if (beginProperties("cull_debug")) {
                sliderIntRow("Box budget", s.boxBudget, 0, 340);
                endProperties();
            }
            ImGui::TextDisabled("%u boxes drawn", stats.boxesDrawn);
            if (stats.boxesDrawn >= static_cast<uint32_t>(s.boxBudget) && s.boxBudget > 0) {
                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.30f, 1.0f),
                                   "Budget reached -- not all bounds shown");
            }
        }
    }
    ImGui::End();
}