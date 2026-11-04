#include "ui/path_modal.h"

#include <imgui.h>

#include <filesystem>

namespace rerevved::tools
{

void OpenPathModal(PathModalState& state, PathModalKind kind, TextureSourceKind sourceKind)
{
    state.kind       = kind;
    state.sourceKind = sourceKind;
    state.diagnostic.clear();
    state.requested      = true;
    state.closeRequested = false;
}

PathModalAction DrawPathModal(PathModalState& state)
{
    constexpr auto title = "Enter path###TexturePathModal";
    if (state.requested)
    {
        ImGui::OpenPopup(title);
        state.requested = false;
    }
    ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        return PathModalAction::None;
    }
    if (state.closeRequested)
    {
        state.closeRequested = false;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return PathModalAction::None;
    }
    const bool source = state.kind == PathModalKind::TextureSource;
    ImGui::TextWrapped("%s", source ? "Import an immutable source from your external editor." : "Choose an existing output parent. The pack ID names a new child directory.");
    if (source)
    {
        if (ImGui::RadioButton("PNG source", state.sourceKind == TextureSourceKind::Png))
        {
            state.sourceKind = TextureSourceKind::Png;
        }
        if (ImGui::RadioButton("DDS source", state.sourceKind == TextureSourceKind::Dds))
        {
            state.sourceKind = TextureSourceKind::Dds;
        }
        const auto extension = std::filesystem::path(state.path.data()).extension().string();
        ImGui::TextWrapped("Extension hint: %s. The selected source kind controls parsing.", extension.empty() ? "none" : extension.c_str());
    }
    ImGui::SetNextItemWidth(-1);
    const bool enter = ImGui::InputText(source ? "Source path" : "Output parent path", state.path.data(), state.path.size(), ImGuiInputTextFlags_EnterReturnsTrue);
    if (!state.diagnostic.empty())
    {
        ImGui::TextWrapped("%s", state.diagnostic.c_str());
    }
    PathModalAction action = PathModalAction::None;
    if (ImGui::Button(source ? "Import Source" : "Use Output Parent") || enter)
    {
        if (state.path[0] == '\0')
        {
            state.diagnostic = "Enter a path before submitting.";
        }
        else
        {
            action = PathModalAction::Submit;
        }
    }
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        action = PathModalAction::Cancel;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    return action;
}

void ClosePathModal(PathModalState& state)
{
    state.closeRequested = true;
}

} // namespace rerevved::tools
