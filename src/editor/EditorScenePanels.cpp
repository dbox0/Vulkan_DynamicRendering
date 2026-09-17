// Scene Save UI
#include "EditorUI.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstring>
#include <system_error>

#include <imgui.h>

#include "UndoHistory.h"

namespace
{
    constexpr const char *kSceneExtension = ".scene";

    constexpr const char *kSaveScenePopup   = "Save Scene As";
    constexpr const char *kOpenScenePopup   = "Open Scene";
    constexpr const char *kDiscardPopup     = "Unsaved Changes";

    std::string displayPath(const std::filesystem::path &path)
    {
        std::error_code ec;
        const std::filesystem::path rel =
            std::filesystem::relative(path, std::filesystem::path(ASSET_DIR), ec);
        if (ec || rel.empty() || rel.generic_string().starts_with("..")) {
            return path.generic_string();
        }
        return rel.generic_string();
    }

    bool samePath(const std::filesystem::path &a, const std::filesystem::path &b)
    {
        if (a.empty() || b.empty()) {
            return false;
        }
        std::error_code ec;
        const bool same = std::filesystem::equivalent(a, b, ec);
        return !ec && same;
    }

    std::string sanitizeSceneName(const char *raw)
    {
        std::string name(raw);
        const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
        name.erase(name.begin(), std::find_if(name.begin(), name.end(), notSpace));
        name.erase(std::find_if(name.rbegin(), name.rend(), notSpace).base(), name.end());

        if (name.size() > std::strlen(kSceneExtension) && name.ends_with(kSceneExtension)) {
            name.resize(name.size() - std::strlen(kSceneExtension));
        }
        if (name.empty() || name == "." || name == ".." ||
            name.find_first_of("/\\") != std::string::npos) {
            return {};
        }
        return name;
    }

    void copyToBuffer(char *buffer, size_t size, const std::string &text)
    {
        const size_t n = std::min(size - 1, text.size());
        std::memcpy(buffer, text.data(), n);
        buffer[n] = '\0';
    }
}

// ---------------------------------------------------------------------------
// state
// ---------------------------------------------------------------------------

bool EditorUI::hasScenePath() const
{
    return m_scenePath && !m_scenePath->empty();
}

bool EditorUI::sceneDirty() const
{
    return m_history && m_history->isDirty();
}

std::string EditorUI::sceneDisplayName() const
{
    return hasScenePath() ? m_scenePath->stem().string() : std::string("Untitled");
}

// ---------------------------------------------------------------------------
// requests
// ---------------------------------------------------------------------------

void EditorUI::saveScene(bool forceSaveAs)
{
    if (!forceSaveAs && hasScenePath()) {
        EditorCommand cmd;
        cmd.kind = EditorCommand::Kind::SaveScene;
        cmd.path = *m_scenePath;
        submit(std::move(cmd));
        return;
    }
    copyToBuffer(m_sceneNameBuffer, sizeof(m_sceneNameBuffer), sceneDisplayName());
    m_sceneSaveAsRequested = true;
}

void EditorUI::openScenePicker()
{
    m_sceneFiles.clear();
    m_sceneFileSelected = -1;

    std::error_code ec;
    auto it = std::filesystem::recursive_directory_iterator(
        ASSET_DIR, std::filesystem::directory_options::skip_permission_denied, ec);
    for (; !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        std::error_code fileEc;
        if (it->is_regular_file(fileEc) && it->path().extension() == kSceneExtension) {
            m_sceneFiles.push_back(it->path());
        }
    }
    std::sort(m_sceneFiles.begin(), m_sceneFiles.end());

    // Preselect the open scene so Enter reloads it.
    for (size_t i = 0; hasScenePath() && i < m_sceneFiles.size(); ++i) {
        if (samePath(m_sceneFiles[i], *m_scenePath)) {
            m_sceneFileSelected = static_cast<int>(i);
        }
    }
    m_sceneOpenRequested = true;
}

void EditorUI::requestSceneAction(SceneAction action, const std::filesystem::path &path)
{
    if (sceneDirty()) {
        m_pendingSceneAction    = action;
        m_pendingScenePath      = path;
        m_sceneDiscardRequested = true;
        return;
    }
    submitSceneAction(action, path);
}

void EditorUI::submitSceneAction(SceneAction action, const std::filesystem::path &path)
{
    EditorCommand cmd;
    switch (action) {
    case SceneAction::New:
        cmd.kind = EditorCommand::Kind::NewScene;
        break;
    case SceneAction::Open:
        cmd.kind = EditorCommand::Kind::LoadScene;
        cmd.path = path;
        break;
    case SceneAction::None:
    default:
        return;
    }
    submit(std::move(cmd));
}

// ---------------------------------------------------------------------------
// drawing
// ---------------------------------------------------------------------------

void EditorUI::drawSceneStatus()
{
    const bool        dirty = sceneDirty();
    const std::string label = "Scene: " + sceneDisplayName() + (dirty ? " *" : "");

    const ImGuiStyle &style = ImGui::GetStyle();
    const float width = ImGui::CalcTextSize(label.c_str()).x;
    const float x     = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x -
                        width - style.ItemSpacing.x;
    if (x > ImGui::GetCursorPosX()) {
        ImGui::SetCursorPosX(x);
    }

    if (dirty) {
        ImGui::TextUnformatted(label.c_str());
    } else {
        ImGui::TextDisabled("%s", label.c_str());
    }

    if (ImGui::BeginItemTooltip()) {
        if (hasScenePath()) {
            ImGui::TextUnformatted(displayPath(*m_scenePath).c_str());
        } else {
            ImGui::TextUnformatted("Not saved yet -- Ctrl+S asks for a name");
        }
        if (dirty) {
            ImGui::TextDisabled("Unsaved changes");
        }
        ImGui::EndTooltip();
    }
}

void EditorUI::drawScenePopups()
{
    // Each popup is opened here, at the root of the ID stack: OpenPopup from
    // inside a menu would use the menu's ID and never match BeginPopupModal.
    drawSaveScenePopup();
    drawOpenScenePopup();
    drawDiscardChangesPopup();
}

void EditorUI::drawSaveScenePopup()
{
    if (m_sceneSaveAsRequested) {
        ImGui::OpenPopup(kSaveScenePopup);
        m_sceneSaveAsRequested = false;
    }
    if (!ImGui::BeginPopupModal(kSaveScenePopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::TextDisabled("Folder");
    ImGui::TextUnformatted(displayPath(m_currentAssetPath).c_str());
    ImGui::TextDisabled("(change it by browsing in the Project panel)");
    ImGui::Dummy(ImVec2(0.0f, 4.0f));

    ImGui::TextDisabled("Name");
    ImGui::SetNextItemWidth(280.0f);
    if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
    }
    const bool entered = ImGui::InputText("##scenename", m_sceneNameBuffer, sizeof(m_sceneNameBuffer),
                                          ImGuiInputTextFlags_EnterReturnsTrue |
                                          ImGuiInputTextFlags_AutoSelectAll);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", kSceneExtension);

    const std::string           name   = sanitizeSceneName(m_sceneNameBuffer);
    const std::filesystem::path target = m_currentAssetPath / (name + kSceneExtension);

    std::error_code ec;
    const bool valid       = !name.empty();
    const bool isCurrent   = valid && hasScenePath() && samePath(target, *m_scenePath);
    const bool overwriting = valid && !isCurrent && std::filesystem::exists(target, ec);

    if (!valid && m_sceneNameBuffer[0] != '\0') {
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f), "Not a valid file name");
    } else if (overwriting) {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.30f, 1.0f),
                           "%s already exists and will be replaced", displayPath(target).c_str());
    }

    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    ImGui::BeginDisabled(!valid);
    const bool confirmed = ImGui::Button(overwriting ? "Overwrite" : "Save", ImVec2(90.0f, 0.0f)) ||
                           (entered && valid);
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(90.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        ImGui::CloseCurrentPopup();
    }

    if (confirmed) {
        EditorCommand cmd;
        cmd.kind = EditorCommand::Kind::SaveScene;
        cmd.path = target;
        submit(std::move(cmd));
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void EditorUI::drawOpenScenePopup()
{
    if (m_sceneOpenRequested) {
        ImGui::OpenPopup(kOpenScenePopup);
        m_sceneOpenRequested = false;
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(360.0f, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
    if (!ImGui::BeginPopupModal(kOpenScenePopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    bool activated = false;
    if (m_sceneFiles.empty()) {
        ImGui::TextDisabled("No .scene files under the asset folder");
    } else if (ImGui::BeginListBox("##scenes", ImVec2(360.0f, 220.0f))) {
        for (size_t i = 0; i < m_sceneFiles.size(); ++i) {
            const bool current  = hasScenePath() && samePath(m_sceneFiles[i], *m_scenePath);
            std::string label   = displayPath(m_sceneFiles[i]);
            if (current) {
                label += "  (open)";
            }
            label += "##" + std::to_string(i);

            const bool selected = m_sceneFileSelected == static_cast<int>(i);
            if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick)) {
                m_sceneFileSelected = static_cast<int>(i);
                activated = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
            }
            if (selected && ImGui::IsWindowAppearing()) {
                ImGui::SetScrollHereY();
            }
        }
        ImGui::EndListBox();
    }

    const bool hasSelection = m_sceneFileSelected >= 0 &&
                              m_sceneFileSelected < static_cast<int>(m_sceneFiles.size());
    if (hasSelection && ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
        activated = true;
    }

    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    ImGui::BeginDisabled(!hasSelection);
    if (ImGui::Button("Open", ImVec2(90.0f, 0.0f))) {
        activated = true;
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(90.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        ImGui::CloseCurrentPopup();
    }

    if (activated && hasSelection) {
        const std::filesystem::path chosen = m_sceneFiles[m_sceneFileSelected];
        ImGui::CloseCurrentPopup();
        // May open the discard prompt; drawDiscardChangesPopup runs after
        // this and opens it at the root, not inside this modal.
        requestSceneAction(SceneAction::Open, chosen);
    }

    ImGui::EndPopup();
}

void EditorUI::drawDiscardChangesPopup()
{
    if (m_sceneDiscardRequested) {
        ImGui::OpenPopup(kDiscardPopup);
        m_sceneDiscardRequested = false;
    }
    if (!ImGui::BeginPopupModal(kDiscardPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    if (m_pendingSceneAction == SceneAction::New) {
        ImGui::TextUnformatted("Start a new scene?");
    } else {
        ImGui::Text("Open %s?", displayPath(m_pendingScenePath).c_str());
    }
    ImGui::Text("'%s' has unsaved changes. Undo history will be cleared.",
                sceneDisplayName().c_str());
    ImGui::Dummy(ImVec2(0.0f, 4.0f));

    bool close = false;

    // Save-then-continue only when there is a file to save to: both commands
    // go out this frame and Application runs them in order (and skips the
    // second if the save fails).
    if (hasScenePath()) {
        if (ImGui::Button("Save", ImVec2(90.0f, 0.0f))) {
            saveScene(false);
            submitSceneAction(m_pendingSceneAction, m_pendingScenePath);
            close = true;
        }
        ImGui::SameLine();
    }
    if (ImGui::Button("Discard", ImVec2(90.0f, 0.0f))) {
        submitSceneAction(m_pendingSceneAction, m_pendingScenePath);
        close = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(90.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        close = true;
    }

    if (close) {
        m_pendingSceneAction = SceneAction::None;
        m_pendingScenePath.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}
