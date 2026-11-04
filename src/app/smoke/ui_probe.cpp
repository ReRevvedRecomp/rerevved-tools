#include "smoke/ui_probe.h"

#include <imgui_internal.h>

#include <vector>

namespace rerevved::tools::ui_probe
{
namespace
{

std::vector<Item> pending;
std::vector<Item> published;

} // namespace

void SetEnabled(bool enabled)
{
    ImGui::GetCurrentContext()->TestEngineHookItems = enabled;
    pending.clear();
    published.clear();
}

void EndFrame()
{
    published.swap(pending);
    pending.clear();
}

std::span<const Item> Items()
{
    return published;
}

} // namespace rerevved::tools::ui_probe

// Required by IMGUI_ENABLE_TEST_ENGINE and called only while TestEngineHookItems is set.
// Windows register without item data, so one registered this frame counts as open.
void ImGuiTestEngineHook_ItemAdd(ImGuiContext* context, ImGuiID id, const ImRect& bb, const ImGuiLastItemData* itemData)
{
    // Keep only the unclipped part of an item, so a click on one cut off by a scrolling
    // panel lands where ImGui can hover it.
    ImRect shown = bb;
    if (itemData != nullptr && context->CurrentWindow != nullptr)
    {
        shown.ClipWithFull(context->CurrentWindow->ClipRect);
    }
    rerevved::tools::ui_probe::pending.push_back({ id, {}, shown.Min, shown.Max, itemData == nullptr });
}

void ImGuiTestEngineHook_ItemInfo(ImGuiContext*, ImGuiID id, const char* label, ImGuiItemStatusFlags flags)
{
    auto& pending = rerevved::tools::ui_probe::pending;
    if (!pending.empty() && pending.back().id == id)
    {
        pending.back().label   = label != nullptr ? label : "";
        pending.back().visible = pending.back().visible || (flags & ImGuiItemStatusFlags_Visible) != 0;
    }
}

void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...)
{
}

const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID)
{
    return nullptr;
}
