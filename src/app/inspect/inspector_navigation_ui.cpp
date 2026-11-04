#include "inspect/inspector_navigation_ui.h"
#include "ui/ui_theme.h"
#include "ui/ui_widgets.h"

#include <imgui.h>

#include <algorithm>
#include <limits>
#include <string>
#include <string_view>

namespace rerevved::tools
{
namespace
{

} // namespace

std::optional<std::size_t> DrawInspectorNavigation(std::span<const std::string> labels,
                                                   InspectorNavigationState&    state,
                                                   const char*                  identityLabel,
                                                   std::size_t                  indexBase)
{
    ImGui::PushID(identityLabel);
    auto matching = FilterInspectorIndices(labels, std::string_view(state.filter.data()));
    if (!state.selectedOriginalIndex && !matching.empty())
    {
        state.selectedOriginalIndex = matching.front();
    }

    const auto& style     = ImGui::GetStyle();
    const auto  width     = std::max(1.0F, ImGui::GetContentRegionAvail().x);
    const auto  countText = std::to_string(matching.size()) + " of " + std::to_string(labels.size());
    const auto  chipWidth = ImGui::CalcTextSize(countText.c_str()).x + 36.0F;
    ImGui::SetNextItemWidth(std::max(1.0F, width - chipWidth - style.ItemSpacing.x));
    if (ImGui::InputTextWithHint("##filter", "Filter retained records", state.filter.data(), state.filter.size()))
    {
        matching = FilterInspectorIndices(labels, std::string_view(state.filter.data()));
        state.navigationError.clear();
    }
    ImGui::SameLine();
    ui::Chip(ui::Tone::Neutral, std::to_string(matching.size()) + " of " + std::to_string(labels.size()), "Matching records", ui::icon::ListChecks);

    const auto previous  = FindInspectorNeighbor(matching, state.selectedOriginalIndex, false);
    const auto next      = FindInspectorNeighbor(matching, state.selectedOriginalIndex, true);
    const auto jumpWidth = 72.0F;
    if (ui::IconButton("previous", ui::icon::ChevronLeft, "Previous matching record", false, previous.has_value()))
    {
        state.selectedOriginalIndex = *previous;
    }
    ImGui::SameLine();
    if (ui::IconButton("next", ui::icon::ChevronRight, "Next matching record", false, next.has_value()))
    {
        state.selectedOriginalIndex = *next;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(std::max(1.0F, ImGui::GetContentRegionAvail().x - jumpWidth - style.ItemSpacing.x));
    const bool submitted = ImGui::InputTextWithHint(
        "##original-index", indexBase == 0 ? "Index starting at 0" : "Ordinal starting at 1", state.originalIndexInput.data(), state.originalIndexInput.size(), ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    const bool jumpClicked = ui::SecondaryButton(nullptr, "Jump", true, {}, jumpWidth);
    if (submitted || jumpClicked)
    {
        state.navigationError = JumpInspectorIndex(state, std::string_view(state.originalIndexInput.data()), indexBase, labels.size())
                                    ? std::string{}
                                    : "Invalid original index. Selection unchanged.";
    }
    if (!state.navigationError.empty())
    {
        ui::TonedText(ui::Tone::Error, state.navigationError);
    }
    if (InspectorSelectionHidden(state, matching))
    {
        ui::TonedText(ui::Tone::Warn, "Selected original index " + std::to_string(*state.selectedOriginalIndex + indexBase) + " is hidden by the filter.");
    }
    ui::VerticalSpace(2.0F);

    const auto rowHeight   = ImGui::GetTextLineHeightWithSpacing();
    const auto tableHeight = std::max(rowHeight * 2.0F, ImGui::GetContentRegionAvail().y * 0.4F);
    if (matching.size() >= static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        ui::TonedText(ui::Tone::Warn, "The inventory is too large for indexed presentation.");
    }
    else if (ImGui::BeginTable("matching-records", 2, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV, ImVec2(width, tableHeight)))
    {
        const auto indexLabel = indexBase == 0 ? "Index" : "Ordinal";
        const auto indexWidth = std::max(ImGui::CalcTextSize(indexLabel).x, ImGui::CalcTextSize(std::to_string(labels.size()).c_str()).x);
        ImGui::TableSetupColumn(indexLabel, ImGuiTableColumnFlags_WidthFixed, indexWidth + style.FramePadding.x * 2.0F);
        ImGui::TableSetupColumn("Retained record", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(matching.size()));
        while (clipper.Step())
        {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
            {
                const auto originalIndex = matching[static_cast<std::size_t>(row)];
                ImGui::PushID(std::to_string(originalIndex).c_str());
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (ImGui::Selectable(std::to_string(originalIndex + indexBase).c_str(),
                                      state.selectedOriginalIndex == originalIndex,
                                      ImGuiSelectableFlags_SpanAllColumns))
                {
                    state.selectedOriginalIndex = originalIndex;
                }
                ImGui::TableNextColumn();
                // Names can contain ImGui label delimiters, so only the original index is a widget ID.
                ImGui::TextUnformatted(labels[originalIndex].data(), labels[originalIndex].data() + labels[originalIndex].size());
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    ImGui::PopID();
    return state.selectedOriginalIndex;
}

} // namespace rerevved::tools
