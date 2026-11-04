#pragma once

#include <imgui.h>

#include <span>
#include <string>

// Records every submitted item's label and rectangle through Dear ImGui's test engine
// hooks so scripted smoke input can target items by label. Off by default.
namespace rerevved::tools::ui_probe
{

struct Item
{
    ImGuiID     id = 0;
    std::string label;
    ImVec2      min;
    ImVec2      max;
    bool        visible = false;
};

void SetEnabled(bool enabled);
// Publishes the items submitted since the previous call. Call once per frame after drawing.
void                                EndFrame();
[[nodiscard]] std::span<const Item> Items();

} // namespace rerevved::tools::ui_probe
