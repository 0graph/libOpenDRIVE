#include "EditorApp.h"
#include "Notices.h"
#include "platform/FileBridge.h"
#include <algorithm>
#include <imgui.h>

namespace odreditor
{
void EditorApp::Draw()
{
    if (auto file = TakeOpenedFile())
    {
        FileName = file->Name;
        Status = "Received " + FileName;
    }
    const auto& io = ImGui::GetIO();
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Open...", "Ctrl+O"))
                ChooseFile();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View"))
        {
            ImGui::MenuItem("About", nullptr, &ShowNotices);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
    if (!io.WantTextInput && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O))
        ChooseFile();
    const float top = ImGui::GetFrameHeight();
    const float bottom = ImGui::GetFrameHeight();
    ImGui::SetNextWindowPos({0, top});
    ImGui::SetNextWindowSize({io.DisplaySize.x, std::max(1.0f, io.DisplaySize.y - top - bottom)});
    ImGui::Begin("Workspace", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    const float width = ImGui::GetContentRegionAvail().x;
    TreeWidth = std::clamp(TreeWidth, 120.0f, std::max(120.0f, width * 0.4f));
    InspectorWidth = std::clamp(InspectorWidth, 160.0f, std::max(160.0f, width * 0.4f));
    ImGui::BeginChild("Document", {TreeWidth, 0});
    ImGui::TextUnformatted("Document");
    ImGui::TextDisabled("Open or drop an XODR file");
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::InvisibleButton("Tree splitter", {4, -1});
    if (ImGui::IsItemActive())
        TreeWidth += io.MouseDelta.x;
    if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    ImGui::SameLine();
    ImGui::BeginChild("Viewport", {std::max(1.0f, ImGui::GetContentRegionAvail().x - InspectorWidth - 20), 0});
    ImGui::TextDisabled("Viewport");
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::InvisibleButton("Inspector splitter", {4, -1});
    if (ImGui::IsItemActive())
        InspectorWidth -= io.MouseDelta.x;
    if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    ImGui::SameLine();
    ImGui::BeginChild("Properties", {0, 0});
    ImGui::TextUnformatted("Properties");
    ImGui::EndChild();
    ImGui::End();
    ImGui::SetNextWindowPos({0, io.DisplaySize.y - bottom});
    ImGui::SetNextWindowSize({io.DisplaySize.x, bottom});
    ImGui::Begin("Status", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextDisabled("%s", Status.c_str());
    ImGui::End();
    if (ShowNotices)
    {
        ImGui::SetNextWindowSize({620, 420}, ImGuiCond_FirstUseEver);
        ImGui::Begin("About", &ShowNotices);
        ImGui::TextWrapped("%s", Notices);
        ImGui::End();
    }
}
} // namespace odreditor
