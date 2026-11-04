#include "map/scenario_editor.h"
#include "ui/ui_widgets.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rerevved::tools
{
namespace
{

int resizeText(ImGuiInputTextCallbackData* data)
{
    auto& text = *static_cast<std::string*>(data->UserData);
    text.resize(static_cast<std::size_t>(data->BufTextLen));
    data->Buf = text.data();
    return 0;
}

bool editText(const char* id, std::string& text, bool multiline = false)
{
    if (multiline)
    {
        const float height = ImGui::GetTextLineHeight() * 3.0F + ImGui::GetStyle().FramePadding.y * 2.0F;
        return ImGui::InputTextMultiline(id, text.data(), text.capacity() + 1, ImVec2(ImGui::CalcItemWidth(), height), ImGuiInputTextFlags_CallbackResize, resizeText, &text);
    }
    return ImGui::InputText(id, text.data(), text.capacity() + 1, ImGuiInputTextFlags_CallbackResize, resizeText, &text);
}

void apply(CreatorWorkspace& workspace, ScenarioEditorState& state, const MapDocument* map, ScenarioDocument document)
{
    const auto result = ApplyScenarioForm(workspace, workspace.Map().revision, std::move(document));
    RefreshMapReadiness(workspace, state, map);
    if (!result)
    {
        state.diagnostic.clear();
        for (const auto prerequisite : result.error().missing)
        {
            state.diagnostic += CreatorPrerequisiteMessage(prerequisite);
            state.diagnostic += '\n';
        }
    }
}

// Each row's tooltip keeps the XML name.
std::string_view variatorLabel(std::string_view name)
{
    constexpr std::array<std::pair<std::string_view, std::string_view>, 35> kLabels{ {
        { "BAGRESSIVE", "B Aggressive" },
        { "BFECUNDITY", "B Fecundity" },
        { "BARBVSGOODY", "Barb vs Goody" },
        { "RESOURCEDENSITY", "Resource Density" },
        { "TECHNOLOGYRATE", "Technology Rate" },
        { "UPGRADERATE", "Upgrade Rate" },
        { "NAVALSUPPORT", "Naval Support" },
        { "MAXCITYSIZE", "Max City Size" },
        { "WONDERSEXPIRE", "Wonders Expire" },
        { "WARANDPEACE", "War and Peace" },
        { "MUSTWINBY", "Must Win By" },
        { "QUICKWIN", "Quick Win" },
        { "SPEEDMODE", "Speed Mode" },
        { "UFOVISIT", "UFO Visit" },
        { "STARTERA", "Start Era" },
        { "STARTSIZE", "Start Size" },
        { "CLIMATE", "Climate" },
        { "CANNOTWINBY", "Cannot Win By" },
        { "NOSPACERACE", "No Space Race" },
        { "STARTGOLD", "Start Gold" },
        { "CARAVANGOLD", "Caravan Gold" },
        { "ATTACKBONUS", "Attack Bonus" },
        { "GREATPEOPLERATE", "Great People Rate" },
        { "QUARTERPRICELIB", "Quarter Price Lib" },
        { "MOREGOLDRESOURCE", "More Gold Resource" },
        { "MORETRADEPERERA", "More Trade per Era" },
        { "DISPLAYCARD", "Display Card" },
        { "STARTYEAR", "Start Year" },
        { "MAPNUMBER", "Map Number" },
        { "STARTLOCME", "Start Loc Me" },
        { "STARTLOC0", "Start Loc 0" },
        { "STARTLOC1", "Start Loc 1" },
        { "STARTLOC2", "Start Loc 2" },
        { "STARTLOC3", "Start Loc 3" },
        { "STARTLOC4", "Start Loc 4" },
    } };
    const auto                                                              found = std::ranges::find(kLabels, name, &std::pair<std::string_view, std::string_view>::first);
    return found != kLabels.end() ? found->second : name;
}

// Keep errors in the tooltip so they do not grow the row. Deduplicate across languages.
std::string markFieldIssues(const ScenarioDocument& document, std::string_view prefix)
{
    std::vector<ScenarioError> shown;
    std::string                messages;
    for (const auto& issue : ValidateScenarioDocument(document))
    {
        if (issue.entryIndex == 0 && issue.field.starts_with(prefix) && !std::ranges::contains(shown, issue.code))
        {
            shown.push_back(issue.code);
            messages += messages.empty() ? "" : "\n";
            messages += ScenarioErrorMessage(issue.code);
        }
    }
    if (!messages.empty())
    {
        const auto min = ImGui::GetItemRectMin();
        const auto max = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRect(ImVec2(min.x - 1.0F, min.y - 1.0F), ImVec2(max.x + 1.0F, max.y + 1.0F), ui::ToneColor(ui::Tone::Error));
    }
    return messages;
}

// The row's field overlaps the row's item, and a multiline field is its own window.
void rowTooltip(const std::string& messages, std::string_view hint)
{
    if ((!messages.empty() || !hint.empty()) && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenOverlapped))
    {
        ui::Tooltip(messages.empty() ? hint : messages);
    }
}

std::optional<ScenarioDocument> editable(CreatorWorkspace& workspace, ScenarioEditorState& state, const MapDocument* map)
{
    const auto& draft = workspace.Map();
    if (!draft.started)
    {
        return std::nullopt;
    }
    if (!draft.scenario || draft.scenario->entries.empty())
    {
        apply(workspace, state, map, ScenarioDocument{ { ScenarioEntry{} } });
        return std::nullopt;
    }
    return *draft.scenario;
}

} // namespace

void DrawScenarioRules(CreatorWorkspace& workspace, ScenarioEditorState& state, const MapDocument* map)
{
    auto document = editable(workspace, state, map);
    if (!document)
    {
        return;
    }
    auto& entry   = document->entries.front();
    bool  changed = false;
    ImGui::PushID("scenario-rules");
    ui::Caption("Xbox variator levels, stored as signed 16 bit numbers. What most of them do in game is not yet known.");
    if (ui::GhostButton(ui::icon::Undo, "Use Defaults"))
    {
        entry.variators = DefaultScenarioVariators();
        changed         = true;
    }
    for (const auto& descriptor : ScenarioVariators())
    {
        ImGui::PushID(static_cast<int>(descriptor.slot));
        const bool isDefault = entry.variators[descriptor.slot] == descriptor.defaultLevel;
        ui::BeginRow("variator", variatorLabel(descriptor.name));
        const float field = std::min(ImGui::CalcItemWidth(), 160.0F);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::CalcItemWidth() - field) * 0.5F);
        ImGui::SetNextItemWidth(field);
        changed |= ImGui::InputScalar("##variator", ImGuiDataType_S16, &entry.variators[descriptor.slot]);
        if (isDefault)
        {
            ImGui::SameLine(0.0F, 10.0F);
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(ui::ToVec4(ui::color::Muted), "Default");
        }
        ui::EndRow();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        {
            ui::Tooltip(std::string(descriptor.name) + ", slot " + std::to_string(descriptor.slot) + ", default " + std::to_string(descriptor.defaultLevel));
        }
        ImGui::PopID();
    }
    ImGui::PopID();
    if (changed)
    {
        apply(workspace, state, map, std::move(*document));
    }
}

void DrawScenarioDetails(CreatorWorkspace& workspace, ScenarioEditorState& state, const MapDocument* map)
{
    auto document = editable(workspace, state, map);
    if (!document)
    {
        return;
    }
    auto& entry   = document->entries.front();
    bool  changed = false;
    ImGui::PushID("scenario-details");
    auto title = entry.title.front();
    ui::BeginRow("title", "Title");
    if (editText("##title", title))
    {
        entry.title.fill(title);
        changed = true;
    }
    const auto titleIssues = markFieldIssues(*document, "TITLE/");
    ui::EndRow();
    rowTooltip(titleIssues, "Shown in the game's scenario list in every language, and the map pack's name. Plain printable ASCII, no markup.");
    auto description = entry.description.front();
    ui::BeginRow("description", "Description");
    if (editText("##description", description, true))
    {
        entry.description.fill(description);
        changed = true;
    }
    const auto descriptionIssues = markFieldIssues(*document, "DESCRIPTION/");
    ui::EndRow();
    rowTooltip(descriptionIssues, {});
    ui::BeginRow("multiplayer", "Multiplayer");
    constexpr std::array<const char*, 2> kOffOn{ "Off", "On" };
    int                                  multiplayer = entry.multiplayer ? 1 : 0;
    if (ui::Stepper("multiplayer", kOffOn, multiplayer))
    {
        entry.multiplayer = multiplayer == 1;
        changed           = true;
    }
    ui::EndRow();
    if (!state.diagnostic.empty())
    {
        ui::TonedText(ui::Tone::Warn, state.diagnostic);
    }
    ImGui::PopID();
    if (changed)
    {
        apply(workspace, state, map, std::move(*document));
    }
}

void DrawScenarioAdvanced(CreatorWorkspace& workspace, ScenarioEditorState& state, const MapDocument* map)
{
    auto document = editable(workspace, state, map);
    if (!document)
    {
        return;
    }
    auto& entry   = document->entries.front();
    bool  changed = false;
    ImGui::PushID("scenario-advanced");
    ui::BeginRow("scenario-id", "Scenario ID");
    changed |= ImGui::InputScalar("##scenario-id", ImGuiDataType_S32, &entry.id);
    const auto idIssues = markFieldIssues(*document, "ID");
    ui::EndRow();
    rowTooltip(idIssues, "100 or higher. The game's own scenarios use lower IDs.");
    ui::BeginRow("map-letter", "Map Letter");
    changed |= editText("##map-letter", entry.map);
    const auto letterIssues = markFieldIssues(*document, "MAP");
    ui::EndRow();
    rowTooltip(letterIssues, "One letter, A to Z, that no other installed map pack uses.");
    ImGui::PopID();
    if (changed)
    {
        apply(workspace, state, map, std::move(*document));
    }
}

} // namespace rerevved::tools
