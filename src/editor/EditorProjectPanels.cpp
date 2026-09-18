#ifndef ASSET_DIR
#define ASSET_DIR "./"
#endif

#include "EditorUI.h"
#include "EditorCommands.h"

#include <volk.h>
#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "../assets/AssetTypes.h"
#include "../assets/Material.h"
#include "../render/resources/ResourceStore.h"

namespace
{
    // Case-insensitive substring test. ASCII-only

    bool matchesFilter(std::string_view haystack, std::string_view needle)
    {
        if (needle.empty()) {
            return true;
        }
        if (needle.size() > haystack.size()) {
            return false;
        }
        const auto lower = [](char c) {
            return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        };
        for (size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
            size_t j = 0;
            while (j < needle.size() && lower(haystack[i + j]) == lower(needle[j])) {
                ++j;
            }
            if (j == needle.size()) {
                return true;
            }
        }
        return false;
    }
}

bool EditorUI::searchBar(const char *id, std::string &filter)
{
    ImGui::PushID(id);
    char buffer[128];
    std::snprintf(buffer, sizeof(buffer), "%s", filter.c_str());

    const float clearWidth = ImGui::GetFrameHeight();
    ImGui::SetNextItemWidth(-(clearWidth + ImGui::GetStyle().ItemSpacing.x));
    if (ImGui::InputTextWithHint("##search", "Search...", buffer, sizeof(buffer))) {
        filter = buffer;
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(filter.empty());
    if (ImGui::Button("x", ImVec2(clearWidth, 0.0f))) {
        filter.clear();
    }
    ImGui::EndDisabled();

    ImGui::PopID();
    return !filter.empty();
}

namespace
{
    struct ProjectItem
    {
        std::string label;
        ImTextureID thumbnail = 0;                      // 0 -> use colour
        ImVec4      color{ 0.3f, 0.3f, 0.32f, 1.0f };
        const char *badge = nullptr;                    // "DIR", "GLTF", "MAT"
        uint32_t    payload = 0;                        // material id, or index into the entry list
        bool        renaming = false;                   // draw the label as an edit field
    };

    // Below this val : list view takes over.
    constexpr float ListThreshold = 26.0f;

    struct ProjectItemActions
    {
        std::function<bool(const ProjectItem &)> isSelected;
        std::function<void(const ProjectItem &)> onClick;
        std::function<void(const ProjectItem &)> onActivate;     // double click
        std::function<void(const ProjectItem &)> onDragSource;   // called right after the widget
        std::function<void(const ProjectItem &)> onContextMenu;  // right-click on the item itself

        std::function<bool(const ProjectItem &)> drawRename;
    };

    void drawProjectItemsGrid(const std::vector<ProjectItem> &items, float thumbSize,
                              const ProjectItemActions &actions)
    {
        const float cellSize = thumbSize + 16.0f;
        const float width    = ImGui::GetContentRegionAvail().x;
        const int   columns  = std::max(1, static_cast<int>(width / cellSize));

        if (!ImGui::BeginTable("ProjectGrid", columns)) {
            return;
        }

        for (const ProjectItem &item : items) {
            ImGui::TableNextColumn();
            ImGui::PushID(static_cast<int>(item.payload));

            const bool   selected = actions.isSelected && actions.isSelected(item);
            const ImVec2 origin   = ImGui::GetCursorScreenPos();

            if (selected) {
                ImGui::GetWindowDrawList()->AddRectFilled(
                    ImVec2(origin.x - 4.0f, origin.y - 4.0f),
                    ImVec2(origin.x + thumbSize + 4.0f,
                           origin.y + thumbSize + ImGui::GetTextLineHeight() * 2.0f),
                    ImGui::GetColorU32(ImGuiCol_ButtonActive), 4.0f);
            }

            bool clicked = false;
            if (item.thumbnail) {
                clicked = ImGui::ImageButton("##thumb", item.thumbnail,
                                             ImVec2(thumbSize, thumbSize));
            } else {
                clicked = ImGui::ColorButton("##thumb", item.color,
                                             ImGuiColorEditFlags_NoTooltip |
                                             ImGuiColorEditFlags_NoDragDrop,
                                             ImVec2(thumbSize, thumbSize));
            }

            if (actions.onDragSource) {
                actions.onDragSource(item);
            }

            const bool activated = ImGui::IsItemHovered() &&
                                   ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

            if (actions.onContextMenu) {
                actions.onContextMenu(item);
            }

            if (item.badge && !item.thumbnail) {
                const ImVec2 textSize = ImGui::CalcTextSize(item.badge);
                ImGui::GetWindowDrawList()->AddText(
                    ImVec2(origin.x + (thumbSize - textSize.x) * 0.5f,
                           origin.y + (thumbSize - textSize.y) * 0.5f),
                    ImGui::GetColorU32(ImGuiCol_Text), item.badge);
            }

            if (item.renaming && actions.drawRename) {
                ImGui::PushItemWidth(thumbSize);
                actions.drawRename(item);
                ImGui::PopItemWidth();
            } else {
                ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + thumbSize);
                ImGui::TextUnformatted(item.label.c_str());
                ImGui::PopTextWrapPos();
            }

            if (activated && actions.onActivate) {
                actions.onActivate(item);
            } else if (clicked && actions.onClick) {
                actions.onClick(item);
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    void drawProjectItemsList(const std::vector<ProjectItem> &items,
                              const ProjectItemActions &actions)
    {
        const float rowHeight = ImGui::GetTextLineHeight();

        for (const ProjectItem &item : items) {
            ImGui::PushID(static_cast<int>(item.payload));

            if (item.thumbnail) {
                ImGui::Image(item.thumbnail, ImVec2(rowHeight, rowHeight));
            } else {
                ImGui::ColorButton("##swatch", item.color,
                                   ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
                                   ImVec2(rowHeight, rowHeight));
            }
            ImGui::SameLine();

            if (item.renaming && actions.drawRename) {
                actions.drawRename(item);
                ImGui::PopID();
                continue;
            }

            std::string row;
            if (item.badge) {
                row += "[";
                row += item.badge;
                row += "] ";
            }
            row += item.label;

            const bool selected = actions.isSelected && actions.isSelected(item);
            const bool clicked  = ImGui::Selectable(row.c_str(), selected,
                                                    ImGuiSelectableFlags_AllowDoubleClick);

            if (actions.onDragSource) {
                actions.onDragSource(item);
            }
            if (actions.onContextMenu) {
                actions.onContextMenu(item);
            }

            if (clicked) {
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    if (actions.onActivate) {
                        actions.onActivate(item);
                    }
                } else if (actions.onClick) {
                    actions.onClick(item);
                }
            }
            ImGui::PopID();
        }
    }

    // Lowercased extension
    std::string lowerExtension(const std::filesystem::path &path)
    {
        std::string ext = path.extension().string();
        for (char &c : ext) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return ext;
    }

    bool isImageExtension(const std::string &ext)
    {
        return ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
               ext == ".tga" || ext == ".bmp";
    }
}

void EditorUI::drawProjectPanel(const ResourceStore &resources)
{
    if (ImGui::BeginMenuBar()) {
        if (ImGui::MenuItem("Assets", nullptr, m_projectTab == ProjectTab::Assets)) {
            m_projectTab = ProjectTab::Assets;
        }
        if (ImGui::MenuItem("Materials", nullptr, m_projectTab == ProjectTab::Materials)) {
            m_projectTab = ProjectTab::Materials;
        }
        if (ImGui::MenuItem("Textures", nullptr, m_projectTab == ProjectTab::Textures)) {
            m_projectTab = ProjectTab::Textures;
        }

        if (m_projectTab != ProjectTab::Assets) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100.0f);

            const char *labels[] = { "All", "Project", "Imported" };
            int current = static_cast<int>(m_originFilter);
            if (ImGui::Combo("##origin", &current, labels, IM_ARRAYSIZE(labels))) {
                m_originFilter = static_cast<OriginFilter>(current);
            }
        }
        constexpr float sliderWidth = 110.0f;
        const float     sliderX     = ImGui::GetContentRegionMax().x - sliderWidth;
        if (sliderX > ImGui::GetCursorPosX()) {
            ImGui::SameLine(sliderX);
            ImGui::SetNextItemWidth(sliderWidth);
            ImGui::SliderFloat("##zoom", &m_thumbnailSize, 16.0f, 128.0f, "");
            if (ImGui::BeginItemTooltip()) {
                ImGui::TextUnformatted("Thumbnail size -- drag fully left for list view");
                ImGui::EndTooltip();
            }
        }
        m_projectView = m_thumbnailSize <= ListThreshold ? ProjectView::List : ProjectView::Grid;

        ImGui::EndMenuBar();
    }

    switch (m_projectTab) {
    case ProjectTab::Assets:    drawAssetsTab();            break;
    case ProjectTab::Materials: drawMaterialsTab(resources); break;
    case ProjectTab::Textures:  drawTexturesTab(resources);  break;
    }
}

bool EditorUI::passesOriginFilter(AssetOrigin origin) const
{
    switch (m_originFilter) {
    case OriginFilter::Project:  return origin == AssetOrigin::Project;
    case OriginFilter::Imported: return origin == AssetOrigin::Imported;
    case OriginFilter::All:
    default:                     return true;
    }
}

void EditorUI::drawAssetsTab()
{
    ImGui::TextDisabled("Current Path:");
    ImGui::SameLine();
    ImGui::TextUnformatted(m_currentAssetPath.string().c_str());

    const std::filesystem::path rootPath   = std::filesystem::absolute(ASSET_DIR);
    const std::filesystem::path currentAbs = std::filesystem::absolute(m_currentAssetPath);

    if (currentAbs != rootPath) {
        if (ImGui::Button("<- Up")) {
            m_currentAssetPath = m_currentAssetPath.parent_path();
            m_selectedAsset.clear();
        }
        ImGui::Separator();
    }

    const bool filtering = searchBar("assetsearch", m_assetFilter);
    ImGui::Separator();

    // Built before anything is drawn: the grid needs the count to lay out
    // columns, and payload indexes m_assetEntries.
    m_assetEntries.clear();
    std::vector<ProjectItem> items;

    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator(m_currentAssetPath, ec)) {
        const std::string filename = entry.path().filename().string();
        if (!matchesFilter(filename, m_assetFilter)) {
            continue;
        }

        ProjectItem item;
        item.label    = filename;
        item.payload  = static_cast<uint32_t>(m_assetEntries.size());
        item.renaming = isRenamingAsset(entry.path());

        if (entry.is_directory()) {
            item.badge = "DIR";
            item.color = ImVec4(0.32f, 0.30f, 0.22f, 1.0f);
        } else {
            const std::string ext = lowerExtension(entry.path());
            if (ext == ".gltf" || ext == ".glb") {
                item.badge = "GLTF";
                item.color = ImVec4(0.20f, 0.28f, 0.36f, 1.0f);
            } else if (ext == ".mat") {
                item.badge = "MAT";
                item.color = ImVec4(0.30f, 0.22f, 0.34f, 1.0f);
            } else if (ext == ".scene") {
                item.badge = "SCENE";
                item.color = ImVec4(0.36f, 0.26f, 0.18f, 1.0f);
            } else if (isImageExtension(ext)) {
                // No thumbnail: a file on disk is not a loaded texture, and
                // decoding every image in a folder just to browse it would
                // upload a lot of VRAM nobody asked for.
                item.badge = "IMG";
                item.color = ImVec4(0.22f, 0.30f, 0.24f, 1.0f);
            } else {
                item.badge = "FILE";
            }
        }

        m_assetEntries.push_back(entry.path());
        items.push_back(std::move(item));
    }

    ProjectItemActions actions;
    actions.isSelected = [this](const ProjectItem &item)
    {
        return !m_selectedAsset.empty() && m_assetEntries[item.payload] == m_selectedAsset;
    };
    actions.onClick = [this](const ProjectItem &item)
    {
        m_selectedAsset = m_assetEntries[item.payload];
    };
    actions.onActivate = [this](const ProjectItem &item)
    {
        const std::filesystem::path &path = m_assetEntries[item.payload];
        m_selectedAsset = path;

        std::error_code dirEc;
        if (std::filesystem::is_directory(path, dirEc)) {
            m_currentAssetPath = path;
            m_selectedAsset.clear();
            return;
        }

        const std::string ext = lowerExtension(path);
        if (ext == ".gltf" || ext == ".glb") {
            EditorCommand cmd;
            cmd.kind = EditorCommand::Kind::LoadModel;
            cmd.path = path;
            submit(std::move(cmd));
        } else if (ext == ".mat") {
            EditorCommand cmd;
            cmd.kind = EditorCommand::Kind::LoadMaterial;
            cmd.path = path;
            submit(std::move(cmd));
        } else if (ext == ".scene") {
            requestSceneAction(SceneAction::Open, path);
        }
    };
    actions.onDragSource = [this](const ProjectItem &item)
    {
        const std::filesystem::path &path = m_assetEntries[item.payload];
        if (isImageExtension(lowerExtension(path))) {
            beginAssetDrag(path);
        }
    };
    actions.onContextMenu = [this](const ProjectItem &item)
    {
        if (!ImGui::BeginPopupContextItem()) {
            return;
        }
        if (ImGui::MenuItem("Rename", "F2")) {
            beginRenameAsset(m_assetEntries[item.payload]);
        }
        ImGui::EndPopup();
    };
    actions.drawRename = [this](const ProjectItem &item)
    {
        if (!renameField("##assetrename")) {
            return false;
        }
        EditorCommand cmd;
        cmd.kind = EditorCommand::Kind::RenameAsset;
        cmd.path = m_assetEntries[item.payload];
        cmd.name = m_renameBuffer;
        submit(std::move(cmd));
        cancelRename();
        return true;
    };

    ImGui::BeginChild("AssetList");
    if (m_projectView == ProjectView::Grid) {
        drawProjectItemsGrid(items, m_thumbnailSize, actions);
    } else {
        drawProjectItemsList(items, actions);
    }

    if (items.empty()) {
        ImGui::TextDisabled(filtering ? "No matches" : "Empty folder");
    }

    drawAssetContextMenu();
    ImGui::EndChild();
}

void EditorUI::drawAssetContextMenu()
{
    // NoOpenOverItems so right-clicking a file does not get the create menu
    if (!ImGui::BeginPopupContextWindow("assetcontext",
                                        ImGuiPopupFlags_MouseButtonRight |
                                        ImGuiPopupFlags_NoOpenOverItems)) {
        return;
    }

    if (ImGui::MenuItem("New Folder")) {
        EditorCommand cmd;
        cmd.kind = EditorCommand::Kind::CreateDirectory;
        cmd.path = m_currentAssetPath / "New Folder";
        submit(std::move(cmd));
    }

    if (ImGui::MenuItem("New Material")) {
        EditorCommand cmd;
        cmd.kind = EditorCommand::Kind::CreateMaterial;
        cmd.path = m_currentAssetPath / "New Material.mat";
        submit(std::move(cmd));
    }

    ImGui::EndPopup();
}

void EditorUI::drawMaterialsContextMenu()
{
    if (!ImGui::BeginPopupContextWindow("materialcontext",
                                        ImGuiPopupFlags_MouseButtonRight |
                                        ImGuiPopupFlags_NoOpenOverItems)) {
        return;
    }

    if (ImGui::MenuItem("New Material")) {
        EditorCommand cmd;
        cmd.kind = EditorCommand::Kind::CreateMaterial;
        submit(std::move(cmd));
    }

    ImGui::EndPopup();
}

void EditorUI::drawMaterialsTab(const ResourceStore &resources)
{
    const bool filtering = searchBar("matsearch", m_materialFilter);
    ImGui::Separator();

    std::vector<ProjectItem> items;
    for (uint32_t i = 1; i <= resources.materialCount(); ++i) {
        const Material &mat = resources.material(i);

        if (!passesOriginFilter(resources.materialInfo(i).origin)) {
            continue;
        }

        const std::string name = mat.name.empty() ? "Material " + std::to_string(i) : mat.name;
        if (!matchesFilter(name, m_materialFilter)) {
            continue;
        }

        ProjectItem item;
        item.payload  = i;
        item.renaming = isRenaming(RenameTarget::Material, i);
        item.label    = resources.materialInfo(i).dirty ? name + " *" : name;
        item.color   = ImVec4(mat.baseColorFactor.r, mat.baseColorFactor.g,
                              mat.baseColorFactor.b, 1.0f);

        if (mat.baseColorTexture != 0 && mat.baseColorTexture <= resources.textureCount()) {
            if (const VkDescriptorSet set = texturePreview(resources, mat.baseColorTexture)) {
                item.thumbnail = reinterpret_cast<ImTextureID>(set);
            }
        }
        items.push_back(std::move(item));
    }

    ProjectItemActions actions;
    actions.isSelected = [this](const ProjectItem &item)
    {
        return m_selectionMode == SelectionMode::Material && m_selectedMaterial == item.payload;
    };
    actions.onClick = [this](const ProjectItem &item)
    {
        selectMaterial(item.payload);
    };
    actions.onActivate = actions.onClick;
    actions.onDragSource = [this, &resources](const ProjectItem &item)
    {
        beginMaterialDrag(resources, item.payload);
    };
    actions.onContextMenu = [this, &resources](const ProjectItem &item)
    {
        if (!ImGui::BeginPopupContextItem()) {
            return;
        }
        if (ImGui::MenuItem("Rename", "F2")) {
            beginRename(RenameTarget::Material, item.payload,
                        resources.material(item.payload).name);
        }
        if (ImGui::MenuItem("Save As...")) {
            m_saveAsRequested = true;
            m_saveAsMaterial  = item.payload;
            std::snprintf(m_saveAsBuffer, sizeof(m_saveAsBuffer), "%s",
                          resources.material(item.payload).name.c_str());
        }
        ImGui::EndPopup();
    };
    actions.drawRename = [this, &resources](const ProjectItem &item)
    {
        if (!renameField("##matrename")) {
            return false;
        }
        // A snapshot of the renamed copy
        // Applying it goes through updateMaterial, which marks the material
        // dirty -> asterisk appears
        Material renamed = resources.material(item.payload);
        renamed.name = m_renameBuffer;
        submitModify(EditTarget::forMaterial(item.payload), reflect::toBlob(renamed),
                     "Rename Material");
        cancelRename();
        return true;
    };

    ImGui::BeginChild("MaterialList");
    if (m_projectView == ProjectView::Grid) {
        drawProjectItemsGrid(items, m_thumbnailSize, actions);
    } else {
        drawProjectItemsList(items, actions);
    }

    if (items.empty()) {
        ImGui::TextDisabled(filtering ? "No matches" : "No materials loaded");
    }

    drawMaterialsContextMenu();
    ImGui::EndChild();
}

void EditorUI::drawTexturesTab(const ResourceStore &resources)
{
    const bool filtering = searchBar("texsearch", m_textureFilter);
    ImGui::Separator();

    std::vector<ProjectItem> items;
    for (uint32_t i = 1; i <= resources.textureCount(); ++i) {
        if (!passesOriginFilter(resources.textureOrigin(i))) {
            continue;
        }

        const ResourceStore::Texture   &texture = resources.texture(i);
        const ResourceStore::ImageInfo &info    = resources.imageInfo(texture.imageId);

        const std::string name = info.name.empty() ? "Texture " + std::to_string(i) : info.name;
        if (!matchesFilter(name, m_textureFilter)) {
            continue;
        }

        ProjectItem item;
        item.payload = i;

        item.label = std::filesystem::path(name).filename().string();
        item.badge = "TEX";

        if (const VkDescriptorSet set = texturePreview(resources, i)) {
            item.thumbnail = reinterpret_cast<ImTextureID>(set);
        }
        items.push_back(std::move(item));
    }

    ProjectItemActions actions;
    actions.isSelected = [this](const ProjectItem &item)
    {
        return m_selectionMode == SelectionMode::Texture && m_selectedTexture == item.payload;
    };
    actions.onClick = [this](const ProjectItem &item)
    {
        m_selectionMode   = SelectionMode::Texture;
        m_selectedTexture = item.payload;
        m_selectedNode    = {};
        m_selectedSlot    = 0;
    };
    actions.onActivate = actions.onClick;
    actions.onDragSource = [this, &resources](const ProjectItem &item)
    {
        beginTextureDrag(resources, item.payload);
    };

    ImGui::BeginChild("TextureList");
    if (m_projectView == ProjectView::Grid) {
        drawProjectItemsGrid(items, m_thumbnailSize, actions);
    } else {
        drawProjectItemsList(items, actions);
    }

    if (items.empty()) {
        ImGui::TextDisabled(filtering ? "No matches" : "No textures loaded");
    }
    ImGui::EndChild();
}