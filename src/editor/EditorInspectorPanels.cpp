// ============================================================================
// EditorInspectorPanels.cpp
//
// The mesh / material half of EditorUI's inspector, split out so EditorUI.cpp
// stays about lifetime, theme and the hierarchy.
//
// Written against the authoring Material (assets/Material.h). Every edit goes
// through ResourceStore::updateMaterial(), which re-packs the GpuMaterial and
// writes it through the persistent map -- the next frame shows the change.
// ============================================================================

#include "EditorUI.h"
#include "EditorCommands.h"

#include <volk.h>
#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>

#include <algorithm>
#include <cfloat>
#include <cstdio>

#include "../assets/Material.h"
#include "../render/GeometryStore.h"
#include "../render/ResourceStore.h"


using Texture   = ResourceStore::Texture;
using ImageInfo = ResourceStore::ImageInfo;

namespace
{
    const ImVec4 DimText { 0.55f, 0.55f, 0.60f, 1.0f };
    const ImVec4 WarnText{ 0.95f, 0.72f, 0.25f, 1.0f };

    constexpr float ThumbSize   = 44.0f;
    constexpr float TooltipSize = 256.0f;

    // Label in column 0, cursor left in column 1. Pass fillWidth=false for
    // rows that lay out several items themselves (texture slots).
    void propertyRow(const char *label, bool fillWidth = true)
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::TableSetColumnIndex(1);
        if (fillWidth) {
            ImGui::SetNextItemWidth(-FLT_MIN);
        }
    }

    // Largest w x h rectangle that fits a box x box square, aspect kept.
    ImVec2 fitInto(float box, uint32_t w, uint32_t h)
    {
        if (w == 0 || h == 0) {
            return ImVec2(box, box);
        }
        const float aspect = static_cast<float>(w) / static_cast<float>(h);
        return aspect >= 1.0f ? ImVec2(box, box / aspect) : ImVec2(box * aspect, box);
    }

    bool isSrgb(VkFormat format)
    {
        switch (format) {
            case VK_FORMAT_R8G8B8A8_SRGB:
            case VK_FORMAT_B8G8R8A8_SRGB:
            case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
            case VK_FORMAT_BC3_SRGB_BLOCK:
            case VK_FORMAT_BC7_SRGB_BLOCK:
                return true;
            default:
                return false;
        }
    }

    const char *materialName(const ResourceStore &resources, uint32_t materialId)
    {
        if (materialId == 0 || materialId > resources.materialCount()) {
            return "(default)";
        }
        const std::string &name = resources.material(materialId).name;
        return name.empty() ? "(unnamed)" : name.c_str();
    }
}

// ============================================================================
// texture previews
// ============================================================================

VkDescriptorSet EditorUI::texturePreview(const ResourceStore &resources, uint32_t textureId)
{
    if (const auto it = m_previewSets.find(textureId); it != m_previewSets.end()) {
        return it->second;
    }

    const Texture &texture = resources.texture(textureId);

    // Every bindless image is left in SHADER_READ_ONLY_OPTIMAL after upload,
    // which is what ImGui's sampler binding expects. Using the texture's own
    // sampler means the preview filters/wraps exactly like the material does.
    const VkDescriptorSet set = ImGui_ImplVulkan_AddTexture(
        resources.sampler(texture.samplerId),
        resources.imageView(texture.imageId),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // Cached even if null, so a failure isn't retried (and logged) every frame.
    m_previewSets.emplace(textureId, set);
    return set;
}

void EditorUI::textureSlot(const char *label, uint32_t textureId, bool expectSrgb,
                           const ResourceStore &resources)
{
    propertyRow(label, false);
    ImGui::PushID(label);

    ImDrawList *drawList = ImGui::GetWindowDrawList();
    const ImVec2 origin  = ImGui::GetCursorScreenPos();
    const ImVec2 boxMax(origin.x + ThumbSize, origin.y + ThumbSize);

    // The well is drawn for empty slots too, so every row lines up.
    drawList->AddRectFilled(origin, boxMax, ImGui::GetColorU32(ImGuiCol_FrameBg), 3.0f);
    ImGui::Dummy(ImVec2(ThumbSize, ThumbSize));        // hover target for the tooltip

    const bool hasTexture = textureId != 0 && textureId <= resources.textureCount();
    if (!hasTexture) {
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextDisabled("None");
        ImGui::TextDisabled("factor only");
        ImGui::EndGroup();
        ImGui::PopID();
        return;
    }

    const Texture   &texture = resources.texture(textureId);
    const ImageInfo &info    = resources.imageInfo(texture.imageId);
    const VkDescriptorSet set = texturePreview(resources, textureId);

    if (set) {
        const ImVec2 size = fitInto(ThumbSize, info.width, info.height);
        const ImVec2 pMin(origin.x + (ThumbSize - size.x) * 0.5f,
                          origin.y + (ThumbSize - size.y) * 0.5f);
        drawList->AddImage(reinterpret_cast<ImTextureID>(set), pMin,
                           ImVec2(pMin.x + size.x, pMin.y + size.y));
    }

    const char *name = info.name.empty() ? "(unnamed image)" : info.name.c_str();
    const bool srgb  = isSrgb(info.format);

    if (set && ImGui::BeginItemTooltip()) {
        ImGui::Image(reinterpret_cast<ImTextureID>(set),
                     fitInto(TooltipSize, info.width, info.height));
        ImGui::TextUnformatted(name);
        ImGui::TextDisabled("%u x %u, %s, texture %u / image %u",
                            info.width, info.height, srgb ? "sRGB" : "linear",
                            textureId, texture.imageId);
        ImGui::EndTooltip();
    }

    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextUnformatted(name);                      // table column clips long names
    ImGui::TextDisabled("%u x %u  %s", info.width, info.height, srgb ? "sRGB" : "linear");

    // The bug class from the PBR migration: a colour slot sampling a linear
    // image (or a data slot sampling an sRGB one) looks almost right, so
    // make it loud instead.
    if (srgb != expectSrgb) {
        ImGui::TextColored(WarnText, "expected %s", expectSrgb ? "sRGB" : "linear");
    }
    ImGui::EndGroup();

    ImGui::PopID();
}

// ============================================================================
// mesh
// ============================================================================

void EditorUI::drawMeshSection(uint32_t nodeId, const Mesh &mesh, const ResourceStore &resources)
{
    if (!ImGui::CollapsingHeader("Mesh", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    size_t vertices = 0;
    size_t triangles = 0;
    for (const SubMesh &sm : mesh.subMeshes) {
        vertices  += sm.vertexCount;
        triangles += sm.indexCount / 3;
    }

    ImGui::TextUnformatted(mesh.name.empty() ? "(unnamed mesh)" : mesh.name.c_str());
    ImGui::TextDisabled("%zu submesh%s, %zu verts, %zu tris",
                        mesh.subMeshes.size(), mesh.subMeshes.size() == 1 ? "" : "es",
                        vertices, triangles);

    if (mesh.subMeshes.empty()) {
        return;
    }

    // Up to 6 rows visible, then it scrolls.
    const size_t visibleRows = std::min<size_t>(mesh.subMeshes.size(), 6) + 1;   // + header
    const float  rowHeight   = ImGui::GetTextLineHeight() + ImGui::GetStyle().CellPadding.y * 2.0f;
    const ImVec2 tableSize(0.0f, rowHeight * static_cast<float>(visibleRows) + 2.0f);

    constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
                                      ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;

    if (ImGui::BeginTable("submeshes", 3, flags, tableSize)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("#",        ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Material", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Tris",     ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < mesh.subMeshes.size(); ++i) {
            const SubMesh &sm = mesh.subMeshes[i];
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            char index[24];
            std::snprintf(index, sizeof(index), "%zu", i);
            if (ImGui::Selectable(index, i == m_selectedSubMesh,
                                  ImGuiSelectableFlags_SpanAllColumns)) {
                m_selectedSubMesh = i;
            }

            // The selectable spans all columns, so the whole row is the drop
            // target. This is the one that matters: it is how a single submesh
            // of a multi-material glTF mesh gets retargeted.
            if (const uint32_t dropped = acceptMaterialDrop()) {
                assignMaterial(nodeId, static_cast<uint32_t>(i), dropped);
                m_selectedSubMesh = i;      // show what was just assigned
            }

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(materialName(resources, sm.materialId));

            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%zu", sm.indexCount / 3);
        }
        ImGui::EndTable();
    }
}

// ============================================================================
// material
// ============================================================================

void EditorUI::drawMaterialSection(ResourceStore &resources, uint32_t materialId,
                                   uint32_t nodeId, uint32_t subMesh)
{
    // Not an early return on the header any more: a collapsed header still has
    // to accept a drop, and BeginDragDropTarget only sees the item submitted
    // immediately before it.
    const bool open = ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen);

    if (nodeId != 0) {
        if (const uint32_t dropped = acceptMaterialDrop()) {
            assignMaterial(nodeId, subMesh, dropped);
        }
    }
    if (!open) {
        return;
    }

    // materialId 0 renders with the engine default, so show (and edit) that.
    const bool     isDefault = materialId == 0 || materialId > resources.materialCount();
    const uint32_t id        = isDefault ? resources.defaultMaterialId() : materialId;
    if (id == 0 || id > resources.materialCount()) {
        ImGui::TextDisabled("No material");
        return;
    }

    // Edit a copy; push it back once, only if something changed.
    Material mat = resources.material(id);
    bool changed = false;

    // A swatch rather than the label: Text submits no interactive item, so a
    // drag source on it would need SourceAllowNullID and would then collide
    // with every other nameless label in the panel.
    ImGui::ColorButton("##matswatch",
                       ImVec4(mat.baseColorFactor.r, mat.baseColorFactor.g,
                              mat.baseColorFactor.b, 1.0f),
                       ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
                       ImVec2(18.0f, 18.0f));
    beginMaterialDrag(resources, id);
    ImGui::SameLine();

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(mat.name.empty() ? "(unnamed)" : mat.name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("#%u", id);
    if (isDefault) {
        ImGui::TextColored(DimText, "Engine default -- used by every submesh without a material");
    }
    ImGui::TextColored(DimText, "Materials are shared: edits affect every mesh using this one.");

    // Factors are LINEAR (glTF). _Float shows 0..1 values and keeps hex
    // input from pretending to be sRGB.
    constexpr ImGuiColorEditFlags colorFlags = ImGuiColorEditFlags_Float;

    // ---- surface ------------------------------------------------------------
    ImGui::SeparatorText("Surface");
    beginProperties("mat_surface");
    {
        propertyRow("Alpha");
        const char *modes[] = { "Opaque", "Mask", "Blend" };
        int mode = static_cast<int>(mat.alphaMode);
        if (ImGui::Combo("##alpha", &mode, modes, IM_ARRAYSIZE(modes))) {
            mat.alphaMode = static_cast<AlphaMode>(mode);
            changed = true;
        }

        if (mat.alphaMode == AlphaMode::Mask) {
            propertyRow("Cutoff");
            changed |= ImGui::SliderFloat("##cutoff", &mat.alphaCutoff, 0.0f, 1.0f);
        }

        propertyRow("Two-sided");
        changed |= ImGui::Checkbox("##doublesided", &mat.doubleSided);
    }
    endProperties();

    // ---- base colour --------------------------------------------------------
    ImGui::SeparatorText("Base Color");
    beginProperties("mat_base");
    {
        textureSlot("Texture", mat.baseColorTexture, true, resources);
        propertyRow("Factor");
        changed |= ImGui::ColorEdit4("##basecolor", &mat.baseColorFactor.x,
                                     colorFlags | ImGuiColorEditFlags_AlphaBar);
    }
    endProperties();

    // ---- metallic / roughness -----------------------------------------------
    ImGui::SeparatorText("Metallic / Roughness");
    beginProperties("mat_mr");
    {
        textureSlot("Texture", mat.metallicRoughnessTexture, false, resources);
        if (mat.metallicRoughnessTexture) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("G = roughness, B = metallic");
        }
        propertyRow("Metallic");
        changed |= ImGui::SliderFloat("##metallic", &mat.metallicFactor, 0.0f, 1.0f);
        propertyRow("Roughness");
        changed |= ImGui::SliderFloat("##roughness", &mat.roughnessFactor, 0.0f, 1.0f);
    }
    endProperties();

    // ---- normal -------------------------------------------------------------
    ImGui::SeparatorText("Normal");
    beginProperties("mat_normal");
    {
        textureSlot("Texture", mat.normalTexture, false, resources);
        propertyRow("Scale");
        changed |= ImGui::DragFloat("##normalscale", &mat.normalScale, 0.01f, -4.0f, 4.0f, "%.2f");
    }
    endProperties();

    // ---- occlusion ----------------------------------------------------------
    ImGui::SeparatorText("Occlusion");
    beginProperties("mat_ao");
    {
        textureSlot("Texture", mat.occlusionTexture, false, resources);
        propertyRow("Strength");
        changed |= ImGui::SliderFloat("##aostrength", &mat.occlusionStrength, 0.0f, 1.0f);
    }
    endProperties();

    // ---- emissive -----------------------------------------------------------
    ImGui::SeparatorText("Emissive");
    beginProperties("mat_emissive");
    {
        textureSlot("Texture", mat.emissiveTexture, true, resources);
        propertyRow("Color");
        changed |= ImGui::ColorEdit3("##emissive", &mat.emissiveFactor.x, colorFlags);
        propertyRow("Strength");
        changed |= ImGui::DragFloat("##emissivestrength", &mat.emissiveStrength,
                                    0.05f, 0.0f, 1000.0f, "%.2f");
    }
    endProperties();

    if (changed) {
        resources.updateMaterial(id, mat);
    }
}