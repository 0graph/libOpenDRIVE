#include "Theme.h"
#include <imgui.h>

namespace odreditor
{
void ApplyTheme(float scale)
{
    ImGui::GetStyle() = ImGuiStyle();
    ImGui::StyleColorsLight();
    auto&        style = ImGui::GetStyle();
    const ImVec4 background{0.961f, 0.976f, 1.0f, 1.0f};
    const ImVec4 section{0.835f, 0.898f, 0.980f, 1.0f};
    const ImVec4 input{0.914f, 0.945f, 0.984f, 1.0f};
    const ImVec4 accent{0.086f, 0.514f, 1.0f, 1.0f};
    style.Colors[ImGuiCol_Text] = {0.094f, 0.169f, 0.271f, 1};
    style.Colors[ImGuiCol_TextDisabled] = {0.349f, 0.435f, 0.569f, 1};
    style.Colors[ImGuiCol_WindowBg] = background;
    style.Colors[ImGuiCol_ChildBg] = background;
    style.Colors[ImGuiCol_PopupBg] = background;
    style.Colors[ImGuiCol_FrameBg] = input;
    style.Colors[ImGuiCol_Button] = input;
    style.Colors[ImGuiCol_ButtonHovered] = section;
    style.Colors[ImGuiCol_Header] = section;
    style.Colors[ImGuiCol_HeaderHovered] = section;
    style.Colors[ImGuiCol_HeaderActive] = accent;
    style.Colors[ImGuiCol_CheckMark] = accent;
    style.Colors[ImGuiCol_SliderGrab] = accent;
    style.Colors[ImGuiCol_MenuBarBg] = section;
    style.Colors[ImGuiCol_TitleBgActive] = section;
    style.Colors[ImGuiCol_Border] = {0.655f, 0.706f, 0.780f, 0.55f};
    style.WindowPadding = {PanelPadding, PanelPadding};
    style.FramePadding = {6, 5};
    style.ItemSpacing = {8, 4};
    style.ItemInnerSpacing = {4, 4};
    style.WindowRounding = 3;
    style.FrameRounding = 3;
    style.PopupRounding = 3;
    style.WindowBorderSize = 0;
    style.ChildBorderSize = 0;
    style.ScaleAllSizes(scale);
}
} // namespace odreditor
