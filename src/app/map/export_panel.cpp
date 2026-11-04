#include "map/export_panel.h"
#include "inspect/document_workspace.h"
#include "map/map_edit_model.h"
#include "map/map_wizard.h"
#include "map/scenario_editor.h"
#include "ui/native_dialog.h"
#include "ui/ui_widgets.h"

#include <imgui.h>

#include <algorithm>
#include <cstring>
#include <string_view>
#include <utility>

namespace rerevved::tools
{
namespace
{

template <std::size_t N>
void assign(std::array<char, N>& buffer, std::string_view text)
{
    buffer = {};
    std::memcpy(buffer.data(), text.data(), std::min(text.size(), N - 1));
}

// Name and description come from the scenario in full. The panel's copies only feed the map file.
MapPackSettings settingsFrom(const ExportPanelState& state, const MapDraft& draft)
{
    const auto*     entry = draft.scenario && !draft.scenario->entries.empty() ? &draft.scenario->entries.front() : nullptr;
    MapPackSettings settings;
    settings.id          = state.packId.data();
    settings.name        = entry != nullptr ? entry->title.front() : std::string(state.packName.data());
    settings.version     = state.packVersion.data();
    settings.author      = state.packAuthor.data();
    settings.description = entry != nullptr ? entry->description.front() : std::string(state.packDescription.data());
    settings.slot        = static_cast<std::uint32_t>(std::max(state.slot, 0));
    return settings;
}

bool advancedBlocker(std::string_view blocker)
{
    return std::ranges::any_of(std::array<std::string_view, 5>{ "Pack ID:", "Version:", "Scenario File Slot:", "Scenario ID:", "Map Letter:" }, [blocker](std::string_view row)
                               {
                                   return blocker.starts_with(row);
                               });
}

void drawSaveTo(ExportPanelState& state, bool fileDialogs)
{
    ui::BeginRow("parent", "Save To");
    constexpr float kChoose    = 110.0F;
    const float     shownWidth = std::max(0.0F, ImGui::CalcItemWidth() - kChoose - ImGui::GetStyle().ItemSpacing.x);
    const auto      shown      = state.outputParent.empty() ? std::string("No folder chosen") : PathToUtf8(state.outputParent);
    ImGui::AlignTextToFramePadding();
    const auto clip = ImGui::GetCursorScreenPos();
    ImGui::PushClipRect(clip, ImVec2(clip.x + shownWidth, clip.y + ImGui::GetFrameHeight()), true);
    ImGui::PushStyleColor(ImGuiCol_Text, state.outputParent.empty() ? ui::color::Muted : ui::color::Text);
    ImGui::TextUnformatted(shown.c_str());
    ImGui::PopStyleColor();
    ImGui::PopClipRect();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort) && !state.outputParent.empty())
    {
        ui::Tooltip(shown);
    }
    ImGui::SameLine(0.0F, 0.0F);
    ImGui::SetCursorScreenPos(ImVec2(clip.x + shownWidth + ImGui::GetStyle().ItemSpacing.x, clip.y - ImGui::GetStyle().FramePadding.y));
    if (ui::SecondaryButton(nullptr, "Choose", true, {}, kChoose))
    {
        const auto chosen = fileDialogs ? PickFolderDialog(L"Choose where to save the map pack") : NativeDialogResult(std::unexpected(NativeDialogError::Unsupported));
        if (!chosen && chosen.error() == NativeDialogError::Unsupported)
        {
            OpenPathModal(state.pathModal, PathModalKind::OutputParent);
        }
        else if (!chosen)
        {
            ui::PushToast(ui::Tone::Error, "The folder dialog could not be opened.");
        }
        else if (*chosen)
        {
            state.outputParent = **chosen;
            state.folderPicked = true;
        }
    }
    ui::EndRow();
    if (!state.outputParent.empty() && state.packId[0] != '\0')
    {
        ui::Caption("Creates " + PathToUtf8(state.outputParent / PathFromUtf8(state.packId.data()) / "assets" / "map-pack" / "dlc.fpk") + ".");
    }
}

bool section(const char* label, bool problem)
{
    ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ui::color::Raised);
    ui::PushFont(ui::FontRole::Strong);
    if (problem)
    {
        ImGui::SetNextItemOpen(true);
    }
    const bool open = ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_NoTreePushOnOpen);
    ui::PopFont();
    ImGui::PopStyleColor(2);
    return open;
}

void drawAdvanced(CreatorWorkspace& workspace, ExportPanelState& state, ScenarioEditorState& scenario, const MapDocument* map, bool problem)
{
    if (!section("Advanced", problem))
    {
        return;
    }
    ui::BeginRow("id", "Pack ID");
    if (ImGui::InputTextWithHint("##id", "my-island-map", state.packId.data(), state.packId.size()))
    {
        state.packIdEdited = true;
    }
    ui::EndRow();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    {
        ui::Tooltip("The pack's folder name: lowercase letters, digits and single dashes, up to 39 characters. It follows the title until you change it.");
    }
    ui::BeginRow("version", "Version");
    ImGui::InputTextWithHint("##version", "1.0", state.packVersion.data(), state.packVersion.size());
    ui::EndRow();
    DrawScenarioAdvanced(workspace, scenario, map);
    ui::BeginRow("slot", "Scenario File Slot");
    if (ImGui::InputInt("##slot", &state.slot))
    {
        state.slot = std::clamp(state.slot, static_cast<int>(kMapPackFirstSlot), static_cast<int>(kMapPackLastSlot));
    }
    ui::EndRow();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    {
        ui::Tooltip("Each map pack you enable needs its own slot. The game's packs use 0 to 4, and choosing a Save To folder moves this off any slot its packs use.");
    }
}

void drawChecklist(const std::vector<std::string>& blockers, bool folderChosen, const std::vector<std::string>& warnings)
{
    if (blockers.empty() && folderChosen && warnings.empty())
    {
        return;
    }
    ui::VerticalSpace(6.0F);
    ui::SectionHeading("Before You Export");
    ui::VerticalSpace(2.0F);
    const auto item = [](const std::string& text, ImU32 color)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextUnformatted(ui::ToneIcon(ui::Tone::Warn));
        ImGui::PopStyleColor();
        ImGui::SameLine(0.0F, 10.0F);
        ui::Body(text);
    };
    for (const auto& blocker : blockers)
    {
        item(blocker, ui::color::Highlight);
    }
    if (!folderChosen)
    {
        item("Choose a folder to save to.", ui::color::Highlight);
    }
    for (const auto& warning : warnings)
    {
        item(warning, ui::color::Muted);
    }
}

void drawReceipt(const MapDraft& draft, const MapPackReceipt& receipt, float width)
{
    const auto tone = receipt.published ? ui::Tone::Ok : ui::Tone::Error;
    ui::BeginCard("receipt", width, tone);
    ImGui::PushStyleColor(ImGuiCol_Text, ui::ToneColor(tone));
    ImGui::TextUnformatted(ui::ToneIcon(tone));
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0F, 10.0F);
    ui::Heading(receipt.published ? "Map Pack Exported" : "Export Stopped");
    if (receipt.revision != draft.revision)
    {
        ui::Caption("From an earlier edit of this map.");
    }
    if (!receipt.packDirectory.empty())
    {
        ui::KeyValue("Folder", PathToUtf8(receipt.packDirectory), 110.0F);
    }
    if (receipt.published)
    {
        ui::KeyValue("Map", receipt.mapName + " in scenario slot " + std::to_string(receipt.slot), 110.0F);
    }
    ui::VerticalSpace(2.0F);
    ui::Body(receipt.diagnostic);
    if (ui::GhostButton(ui::icon::Copy, "Copy Receipt"))
    {
        ImGui::SetClipboardText(FormatMapPackReceipt(receipt).c_str());
    }
    ui::EndCard();
}

} // namespace

void SetProtectedPaths(ExportPanelState& state, std::vector<std::filesystem::path> paths)
{
    state.protectedPaths = std::move(paths);
}

void SyncPackWithScenario(ExportPanelState& state, const MapDraft& draft)
{
    const auto* entry = draft.scenario && !draft.scenario->entries.empty() ? &draft.scenario->entries.front() : nullptr;
    if (entry == nullptr)
    {
        return;
    }
    if (std::string_view(state.packName.data()) != std::string_view(entry->title.front()).substr(0, state.packName.size() - 1))
    {
        assign(state.packName, entry->title.front());
        if (!state.packIdEdited)
        {
            assign(state.packId, MapPackIdFromName(entry->title.front()));
        }
    }
    if (std::string_view(state.packDescription.data()) != std::string_view(entry->description.front()).substr(0, state.packDescription.size() - 1))
    {
        assign(state.packDescription, entry->description.front());
    }
}

void DrawExportPanel(CreatorWorkspace& workspace, ExportPanelState& state, ScenarioEditorState& scenario, DocumentWorkspace& activity, const MapDocument* map, bool fileDialogs)
{
    const auto& draft = workspace.Map();
    SyncPackWithScenario(state, draft);
    const auto settings     = settingsFrom(state, draft);
    const auto blockers     = MapPackBlockers(draft, map, settings);
    const bool folderChosen = !state.outputParent.empty();
    state.canExport         = blockers.empty() && folderChosen && map != nullptr;
    state.exportBlocker     = !blockers.empty() ? blockers.front() : folderChosen ? std::string()
                                                                                  : "Choose a folder to save to first.";
    ImGui::PushID(&state);
    if (std::exchange(state.requestExport, false) && state.canExport)
    {
        auto receipt = ExportMapPack(draft, *map, settings, state.outputParent, state.protectedPaths);
        activity.RecordCreatorActivity(receipt.published ? OpenOutcomeKind::ExportPublished : OpenOutcomeKind::ExportFailed, receipt.packDirectory, FormatMapPackReceipt(receipt), receipt.published ? std::string{} : receipt.diagnostic);
        ui::PushToast(receipt.published ? ui::Tone::Ok : ui::Tone::Error, receipt.published ? "Exported " + settings.id + "." : "The map pack was not exported.");
        state.receipt = std::move(receipt);
    }

    if (state.receipt)
    {
        drawReceipt(draft, *state.receipt, ImGui::GetContentRegionAvail().x);
        ui::VerticalSpace(8.0F);
    }
    ui::SectionHeading("Details");
    ui::VerticalSpace(2.0F);
    DrawScenarioDetails(workspace, scenario, map);
    ui::BeginRow("author", "Author");
    ImGui::InputText("##author", state.packAuthor.data(), state.packAuthor.size());
    ui::EndRow();
    drawSaveTo(state, fileDialogs);
    ui::VerticalSpace(6.0F);
    drawAdvanced(workspace, state, scenario, map, std::ranges::any_of(blockers, advancedBlocker));
    if (section("Game Rules", false))
    {
        DrawScenarioRules(workspace, scenario, map);
    }
    // Fewer than two markers is a blocker. Fewer than retail's five is only advice.
    std::vector<std::string> warnings;
    if (map != nullptr)
    {
        if (MapStartMarkerCount(*map) >= 2)
        {
            if (auto advice = MapStartMarkerAdvice(*map))
            {
                warnings.push_back(std::move(*advice));
            }
        }
        for (auto& summary : MapWarningSummaries(*map))
        {
            warnings.push_back(std::move(summary));
        }
    }
    drawChecklist(blockers, folderChosen, warnings);

    const auto action = DrawPathModal(state.pathModal);
    if (action == PathModalAction::Submit)
    {
        state.outputParent = PathFromUtf8(state.pathModal.path.data());
        state.folderPicked = true;
        ClosePathModal(state.pathModal);
    }
    ImGui::PopID();
}

} // namespace rerevved::tools
