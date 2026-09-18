#include "EditorTheme.h"

#include <imgui.h>

namespace editor::ui
{
    void applyTheme()
    {
        ImGui::StyleColorsDark();

        ImGuiStyle& style = ImGui::GetStyle();

        // ---------------------------------------------------------------------
        // Shape
        // ---------------------------------------------------------------------

        style.Alpha = 1.0f;

        style.WindowPadding     = ImVec2(8.0f, 8.0f);
        style.FramePadding      = ImVec2(6.0f, 4.0f);
        style.CellPadding       = ImVec2(4.0f, 3.0f);
        style.ItemSpacing       = ImVec2(6.0f, 5.0f);
        style.ItemInnerSpacing  = ImVec2(4.0f, 4.0f);
        style.IndentSpacing     = 16.0f;

        style.ScrollbarSize     = 12.0f;
        style.GrabMinSize       = 10.0f;

        style.WindowRounding    = 3.0f;
        style.ChildRounding     = 2.0f;
        style.FrameRounding     = 2.0f;
        style.PopupRounding     = 2.0f;
        style.ScrollbarRounding = 2.0f;
        style.GrabRounding      = 2.0f;
        style.TabRounding       = 2.0f;

        style.WindowBorderSize  = 1.0f;
        style.ChildBorderSize   = 1.0f;
        style.FrameBorderSize   = 0.0f;
        style.PopupBorderSize   = 1.0f;

        style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
        style.WindowMenuButtonPosition = ImGuiDir_None;

        // -------------- Palette --------------

        const ImVec4 bg          = ImVec4(0.060f, 0.060f, 0.065f, 1.0f);
        const ImVec4 bgDark      = ImVec4(0.045f, 0.045f, 0.050f, 1.0f);
        const ImVec4 panel       = ImVec4(0.075f, 0.075f, 0.080f, 1.0f);

        const ImVec4 control     = ImVec4(0.110f, 0.110f, 0.115f, 1.0f);
        const ImVec4 hover       = ImVec4(0.145f, 0.145f, 0.250f, 1.0f);
        const ImVec4 active      = ImVec4(0.175f, 0.175f, 0.180f, 1.0f);

        const ImVec4 border      = ImVec4(0.190f, 0.190f, 0.200f, 1.0f);
        const ImVec4 borderLight = ImVec4(0.240f, 0.240f, 0.250f, 1.0f);

        const ImVec4 text        = ImVec4(0.860f, 0.860f, 0.870f, 1.0f);
        const ImVec4 textDim     = ImVec4(0.500f, 0.500f, 0.510f, 1.0f);

        // Blue is intentionally subtle.
        const ImVec4 blue        = ImVec4(0.260f, 0.590f, 0.980f, 1.0f);
        const ImVec4 blueSoft    = ImVec4(0.260f, 0.590f, 0.980f, 0.35f);
        const ImVec4 blueDim     = ImVec4(0.260f, 0.590f, 0.980f, 0.18f);

        ImVec4* c = style.Colors;

        c[ImGuiCol_Text]         = text;
        c[ImGuiCol_TextDisabled] = textDim;


        // Windows / panels

        c[ImGuiCol_WindowBg]     = bg;
        c[ImGuiCol_ChildBg]      = bgDark;
        c[ImGuiCol_PopupBg]      = panel;

        c[ImGuiCol_Border]       = border;
        c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);

        // Inputs / controls

        c[ImGuiCol_FrameBg]        = control;
        c[ImGuiCol_FrameBgHovered] = hover;
        c[ImGuiCol_FrameBgActive]  = active;

        // ---------------------------------------------------------------------
        // Title bars

        c[ImGuiCol_TitleBg]          = bgDark;
        c[ImGuiCol_TitleBgActive]    = panel;
        c[ImGuiCol_TitleBgCollapsed] = bgDark;

        c[ImGuiCol_MenuBarBg] = bgDark;

        // Scrollbars

        c[ImGuiCol_ScrollbarBg]     = bgDark;
        c[ImGuiCol_ScrollbarGrab]   = control;
        c[ImGuiCol_ScrollbarGrabHovered] = hover;
        c[ImGuiCol_ScrollbarGrabActive]  = active;

        // ---------------------------------------------------------------------
        // Buttons

        c[ImGuiCol_Button]        = control;
        c[ImGuiCol_ButtonHovered] = hover;
        c[ImGuiCol_ButtonActive]  = active;

        // ---------------------------------------------------------------------
        // Headers / tree nodes

        c[ImGuiCol_Header]        = control;
        c[ImGuiCol_HeaderHovered] = hover;
        c[ImGuiCol_HeaderActive]  = active;

        // ---------------------------------------------------------------------
        // Checkboxes / sliders


        c[ImGuiCol_CheckMark]      = blue;
        c[ImGuiCol_SliderGrab]     = blueSoft;
        c[ImGuiCol_SliderGrabActive] = blue;

        // ---------------------------------------------------------------------
        // Separators


        c[ImGuiCol_Separator]        = border;
        c[ImGuiCol_SeparatorHovered] = borderLight;
        c[ImGuiCol_SeparatorActive]  = blueSoft;

        // ---------------------------------------------------------------------
        // Resize grips

        c[ImGuiCol_ResizeGrip]        = control;
        c[ImGuiCol_ResizeGripHovered] = hover;
        c[ImGuiCol_ResizeGripActive]  = blue;

        // ---------------------------------------------------------------------
        // Tabs

        c[ImGuiCol_Tab]                = bgDark;
        c[ImGuiCol_TabHovered]         = hover;
        c[ImGuiCol_TabSelected]        = panel;
        c[ImGuiCol_TabSelectedOverline] = blue;
        c[ImGuiCol_TabDimmed]          = bgDark;
        c[ImGuiCol_TabDimmedSelected]  = panel;
        c[ImGuiCol_TabDimmedSelectedOverline] = blueDim;

        // ---------------------------------------------------------------------
        // Tables

        c[ImGuiCol_TableHeaderBg]     = control;
        c[ImGuiCol_TableBorderStrong] = border;
        c[ImGuiCol_TableBorderLight]  = ImVec4(0.130f, 0.130f, 0.135f, 1.0f);

        c[ImGuiCol_TableRowBg]        = ImVec4(0, 0, 0, 0);
        c[ImGuiCol_TableRowBgAlt]     = ImVec4(0.080f, 0.080f, 0.085f, 1.0f);

        // ---------------------------------------------------------------------
        // Selection / navigation


        c[ImGuiCol_TextSelectedBg] = blueDim;
        c[ImGuiCol_NavCursor]      = blue;

        // ---------------------------------------------------------------------
        // Drag & drop

        c[ImGuiCol_DragDropTarget] = blue;

        // ---------------------------------------------------------------------
        // Modal dimming

        c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.55f);
    }
}
