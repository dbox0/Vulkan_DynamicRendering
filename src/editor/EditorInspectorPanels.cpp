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

void EditorUI::textureSlot(const char *label, TextureSlot slot, uint32_t materialId,
                           uint32_t textureId, const ResourceStore &resources)
{
    const bool expectSrgb = slotIsSrgb(slot);

    propertyRow(label, false);
    ImGui::PushID(label);

    ImDrawList *drawList = ImGui::GetWindowDrawList();
    const ImVec2 origin  = ImGui::GetCursorScreenPos();
    const ImVec2 boxMax(origin.x + ThumbSize, origin.y + ThumbSize);

    // The well is drawn for empty slots too, so every row lines up.
    drawList->AddRectFilled(origin, boxMax, ImGui::GetColorU32(ImGuiCol_FrameBg), 3.0f);
    ImGui::Dummy(ImVec2(ThumbSize, ThumbSize));        // hover target and drop target

    // Immediately after the Dummy, so the well itself is what accepts the
    // drop. The command carries the SLOT, not a colour space: whether the
    // file is decoded as sRGB is decided here, by which map it lands in.
    std::filesystem::path dropped;
    uint32_t              droppedTexture = 0;
    if (acceptTextureDrop(dropped, droppedTexture)) {
        EditorCommand cmd;
        cmd.kind        = EditorCommand::Kind::AssignTexture;
        cmd.materialId  = materialId;
        cmd.textureSlot = slot;
        cmd.path        = dropped;
        cmd.textureId   = droppedTexture;
        m_commands.push_back(cmd);
    }

    const bool hasTexture = textureId != 0 && textureId <= resources.textureCount();
    if (!hasTexture) {
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextDisabled("None");
        ImGui::TextDisabled("drop an image here");
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

    // An empty path is the clear: the slot goes back to 0, which the shader
    // already maps to the white default rather than the error texture.
    if (ImGui::SmallButton("Clear")) {
        EditorCommand cmd;
        cmd.kind        = EditorCommand::Kind::AssignTexture;
        cmd.materialId  = materialId;
        cmd.textureSlot = slot;
        m_commands.push_back(cmd);
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


void EditorUI::drawTextureSection(const ResourceStore &resources, uint32_t textureId)
{
    const Texture   &texture = resources.texture(textureId);
    const ImageInfo &info    = resources.imageInfo(texture.imageId);

    const char *name = info.name.empty() ? "(unnamed image)" : info.name.c_str();
    ImGui::TextUnformatted(name);
    ImGui::SameLine();
    ImGui::TextDisabled("#%u", textureId);

    ImGui::Dummy(ImVec2(0.0f, 4.0f));

    if (const VkDescriptorSet set = texturePreview(resources, textureId)) {
        // Fits the panel rather than a fixed box: this is the one place the
        // user is looking at the texture itself rather than at a material that
        // happens to use it.
        const float  available = ImGui::GetContentRegionAvail().x;
        const ImVec2 size      = fitInto(available, info.width, info.height);
        ImGui::Image(reinterpret_cast<ImTextureID>(set), size);
    }

    ImGui::SeparatorText("Image");
    beginProperties("tex_props");
    {
        propertyRow("Size");
        ImGui::Text("%u x %u", info.width, info.height);

        propertyRow("Mips");
        ImGui::Text("%u", info.mipLevels);

        propertyRow("Colour space");
        ImGui::TextUnformatted(isSrgb(info.format) ? "sRGB" : "linear");

        propertyRow("Origin");
        switch (resources.textureOrigin(textureId)) {
        case AssetOrigin::Builtin:  ImGui::TextUnformatted("Built-in");             break;
        case AssetOrigin::Imported: ImGui::TextUnformatted("Imported with a model"); break;
        case AssetOrigin::Project:  ImGui::TextUnformatted("Project asset");         break;
        }

        propertyRow("Image");
        ImGui::Text("%u", texture.imageId);
    }
    endProperties();

    // A linear scan over every material. materialCount() is in the hundreds at
    // worst and this only runs for the one selected texture, so an index would
    // be bookkeeping for nothing -- and bookkeeping that has to stay correct
    // across every material edit.
    ImGui::SeparatorText("Used by");

    size_t users = 0;
    for (uint32_t i = 1; i <= resources.materialCount(); ++i) {
        const Material &mat = resources.material(i);
        const bool uses = mat.baseColorTexture         == textureId ||
                          mat.metallicRoughnessTexture == textureId ||
                          mat.normalTexture            == textureId ||
                          mat.occlusionTexture         == textureId ||
                          mat.emissiveTexture          == textureId;
        if (!uses) {
            continue;
        }
        ++users;

        const std::string label = mat.name.empty() ? "Material " + std::to_string(i) : mat.name;
        ImGui::BulletText("%s", label.c_str());
    }

    if (users == 0) {
        ImGui::TextDisabled("No material references this texture");
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

    // The engine default is regenerated at startup and has no file on disk, so
    // it gets neither a name field nor a save button -- renaming something the
    // next launch rebuilds is a lie, and writing it out would produce a .mat
    // that nothing ever loads.
    const bool isEngineDefault = (id == resources.defaultMaterialId());

    ImGui::AlignTextToFramePadding();
    ImGui::BeginDisabled(isEngineDefault);
    {
        char nameBuffer[128];
        std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", mat.name.c_str());
        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::InputTextWithHint("##matname", "Name", nameBuffer, sizeof(nameBuffer))) {
            mat.name = nameBuffer;
            changed  = true;      // pushed through updateMaterial with the rest
        }
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::TextDisabled("#%u", id);
    const ResourceStore::MaterialInfo &info = resources.materialInfo(id);

    if (info.dirty && !isEngineDefault) {
        ImGui::SameLine();
        ImGui::TextColored(DimText, "*");
    }
    if (!isEngineDefault) {
        ImGui::SameLine();
        if (ImGui::SmallButton(info.sourcePath.empty() ? "Save As..." : "Save")) {
            if (info.sourcePath.empty()) {
                // Nothing to overwrite, so it needs a name and a folder before
                // anything can be written. The modal is drawn from build().
                m_saveAsRequested = true;
                m_saveAsMaterial  = id;
                std::snprintf(m_saveAsBuffer, sizeof(m_saveAsBuffer), "%s", mat.name.c_str());
            } else {
                EditorCommand save;
                save.kind       = EditorCommand::Kind::SaveMaterial;
                save.materialId = id;
                m_commands.push_back(save);
            }
        }
        if (!info.sourcePath.empty() && ImGui::BeginItemTooltip()) {
            ImGui::TextUnformatted(info.sourcePath.string().c_str());
            ImGui::EndTooltip();
        }
    }

    if (isDefault) {
        ImGui::TextColored(DimText, "Engine default -- used by every submesh without a material");
    }
    ImGui::TextColored(DimText, "Materials are shared: edits affect every mesh using this one.");

    // Factors are LINEAR (glTF). _Float shows 0..1 values and keeps hex
    // input from pretending to be sRGB.
    constexpr ImGuiColorEditFlags colorFlags = ImGuiColorEditFlags_Float;

    // ---- surface ------------------------------------------------------------
    ImGui::SeparatorText("Surface");
    ImGui::BeginDisabled(isDefault);
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
        textureSlot("Texture", TextureSlot::BaseColor, id, mat.baseColorTexture, resources);
        propertyRow("Factor");
        changed |= ImGui::ColorEdit4("##basecolor", &mat.baseColorFactor.x,
                                     colorFlags | ImGuiColorEditFlags_AlphaBar);
    }
    endProperties();

    // ---- metallic / roughness -----------------------------------------------
    ImGui::SeparatorText("Metallic / Roughness");
    beginProperties("mat_mr");
    {
        textureSlot("Texture", TextureSlot::MetallicRoughness, id, mat.metallicRoughnessTexture, resources);
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
    if (!isEngineDefault) {
    // ---- normal -------------------------------------------------------------
    ImGui::SeparatorText("Normal");
    beginProperties("mat_normal");
    {
        textureSlot("Texture", TextureSlot::Normal, id, mat.normalTexture, resources);
        propertyRow("Scale");
        changed |= ImGui::DragFloat("##normalscale", &mat.normalScale, 0.01f, -4.0f, 4.0f, "%.2f");
    }
    endProperties();

    // ---- occlusion ----------------------------------------------------------
    ImGui::SeparatorText("Occlusion");
    beginProperties("mat_ao");
    {
        textureSlot("Texture", TextureSlot::Occlusion, id, mat.occlusionTexture, resources);
        propertyRow("Strength");
        changed |= ImGui::SliderFloat("##aostrength", &mat.occlusionStrength, 0.0f, 1.0f);
    }
    endProperties();

    // ---- emissive -----------------------------------------------------------
    ImGui::SeparatorText("Emissive");
    beginProperties("mat_emissive");
    {
        textureSlot("Texture", TextureSlot::Emissive, id, mat.emissiveTexture, resources);
        propertyRow("Color");
        changed |= ImGui::ColorEdit3("##emissive", &mat.emissiveFactor.x, colorFlags);
        propertyRow("Strength");
        changed |= ImGui::DragFloat("##emissivestrength", &mat.emissiveStrength,
                                    0.05f, 0.0f, 1000.0f, "%.2f");
    }
    endProperties();
    }
    ImGui::EndDisabled();
    if (changed) {
        resources.updateMaterial(id, mat);
    }
}