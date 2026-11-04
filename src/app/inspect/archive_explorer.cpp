#include "inspect/archive_explorer.h"
#include "fpk_extraction.h"
#include "inspect/asset_inspector.h"
#include "inspect/document_presentation.h"
#include "inspect/gfx_inspector.h"
#include "inspect/inspection_format.h"
#include "inspect/mp3_preview.h"
#include "inspect/nif_inspector.h"
#include "map/map_preview.h"
#include "ui/native_dialog.h"
#include "ui/ui_theme.h"
#include "ui/ui_widgets.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <string>
#include <utility>

namespace rerevved::tools
{
namespace
{

constexpr std::array<const char*, 6> kOpenFormatNames{
    "Choose parser", "DDS texture", "GFX movie", "Gamebryo NIF", "MP3 audio", "Xbox DLC map"
};

constexpr const char* kExtractSheet = "Extract selected FPK entry";

constexpr std::array<const char*, 5> kFormatNames{
    "DDS texture", "GFX movie", "Gamebryo NIF", "MP3 audio", "Xbox DLC map"
};

FpkEntryKind inspectEntryKind(const FpkDocument& document, std::size_t selectedEntry)
{
    const auto kind = InspectFpkEntryKind(document, selectedEntry);
    return kind.value_or(FpkEntryKind::Unknown);
}

std::string extractionErrorMessage(FpkExtractionError error)
{
    switch (error)
    {
        case FpkExtractionError::InvalidSelection:
        case FpkExtractionError::InvalidEntryRange:
            std::unreachable();
        case FpkExtractionError::DestinationExists:
            return "The extraction destination already exists and was not overwritten.";
        case FpkExtractionError::OpenFailed:
            return "The extraction destination could not be created.";
        case FpkExtractionError::WriteFailed:
            return "The selected entry could not be written completely.";
        case FpkExtractionError::FinalizationFailed:
            return "The extracted file could not be finalized.";
    }
    std::unreachable();
}

void drawOpenedInspector(ArchiveExplorerState& state, DdsViewer& ddsViewer)
{
    if (!state.openedDocument)
    {
        return;
    }

    if (ui::SecondaryButton(ui::icon::Close, "Close Opened Entry"))
    {
        ddsViewer.Clear();
        CloseOpenedArchiveEntry(state);
        return;
    }
    ui::VerticalSpace(4.0F);

    const auto& data = state.openedDocument->data;
    if (const auto* gfx = std::get_if<GfxDocument>(&data))
    {
        DrawGfxInspector(gfx, {}, state.gfxInspector);
    }
    else if (const auto* nif = std::get_if<NifDocument>(&data))
    {
        DrawNifInspector(nif, {}, state.nifInspector);
    }
    else if (const auto* dds = std::get_if<DdsDocument>(&data))
    {
        DrawDdsInspector(*dds);
    }
    else if (const auto* mp3 = std::get_if<Mp3Document>(&data))
    {
        DrawMp3Inspector(*mp3);
    }
    else if (const auto* map = std::get_if<MapDocument>(&data))
    {
        DrawMapInspector(*map, state.mapInspector);
    }
}

void drawExtractionPopup(const FpkDocument& document, ArchiveExplorerState& state)
{
    if (!ui::BeginSheet(kExtractSheet, ui::icon::Download, "Extract Entry " + std::to_string(*state.selectedEntry + 1), 520.0F))
    {
        return;
    }
    ui::Muted("Enter a new destination path. Existing paths are never overwritten.");
    ui::VerticalSpace(4.0F);
    ImGui::SetNextItemWidth(-1.0F);
    if (ImGui::IsWindowAppearing())
    {
        ImGui::SetKeyboardFocusHere();
    }
    const bool submitted = ImGui::InputTextWithHint("##fpk-extraction-path", "Destination file path", state.extractionPath.data(), state.extractionPath.size(), ImGuiInputTextFlags_EnterReturnsTrue);
    if (!state.extractionResult.empty())
    {
        ui::TonedText(ui::Tone::Error, state.extractionResult);
    }
    ui::SheetFooter(2);
    if (ui::SheetButton("Extract", true) || submitted)
    {
        auto result = ExtractFpkEntry(document.bytes, document.index, *state.selectedEntry, PathFromUtf8(state.extractionPath.data()));
        if (result)
        {
            state.extractionResult = "Extracted entry " + std::to_string(*state.selectedEntry + 1) + " to a new destination.";
            ui::PushToast(ui::Tone::Ok, state.extractionResult);
            state.extractionPath.fill('\0');
            ImGui::CloseCurrentPopup();
        }
        else
        {
            state.extractionResult = extractionErrorMessage(result.error());
        }
    }
    ImGui::SameLine();
    if (ui::SheetButton("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        ImGui::CloseCurrentPopup();
    }
    ui::EndSheet();
}

} // namespace

std::string_view ArchiveFormatName(FpkEntryFormat format)
{
    return kFormatNames[static_cast<std::size_t>(format)];
}

void DrawArchiveInspector(const FpkDocument*    document,
                          ArchiveExplorerState& state,
                          std::string_view      error,
                          DdsViewer&            ddsViewer)
{
    ui::Heading("FPK archive");
    if (!document)
    {
        ui::TonedText(ui::Tone::Error, error);
        return;
    }
    ui::KeyValue("Version", std::to_string(document->index.version), 96.0F);
    ui::KeyValue("Entries", std::to_string(document->index.entries.size()), 96.0F);
    ui::KeyValue("Header", FormatBytes(document->index.headerUnknown), 96.0F);
    ui::Divider();

    if (document->index.entries.empty())
    {
        ui::Muted("This archive contains no entries.");
        return;
    }
    if (!state.selectedEntry)
    {
        ui::Muted("Select a numbered entry to inspect it.");
        return;
    }

    const auto& entry      = document->index.entries[*state.selectedEntry];
    const auto  entryRange = FormatFpkEntryRange(*state.selectedEntry + 1, entry);
    ui::Heading("Entry " + std::to_string(*state.selectedEntry + 1));
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0F, ImGui::GetContentRegionAvail().x - 32.0F));
    if (ui::IconButton("copy-range", ui::icon::Copy, "Copy byte range"))
    {
        ImGui::SetClipboardText(entryRange.c_str());
        state.metadataResult = "Copied selected entry byte range.";
        ui::PushToast(ui::Tone::Ok, state.metadataResult);
    }
    const auto hex = [](std::uint64_t value)
    {
        std::array<char, 24> text{};
        std::snprintf(text.data(), text.size(), "0x%08llX", static_cast<unsigned long long>(value));
        return std::string(text.data());
    };
    const auto end = static_cast<std::uint64_t>(entry.offset) + entry.size;
    ui::KeyValue("Offset", std::to_string(entry.offset) + "  " + hex(entry.offset), 96.0F);
    ui::KeyValue("Size", std::to_string(entry.size) + " bytes  " + hex(entry.size), 96.0F);
    ui::KeyValue("End", std::to_string(end) + "  " + hex(end) + " (exclusive)", 96.0F);
    ui::KeyValue("Header", ArchiveEntryHintName(inspectEntryKind(*document, *state.selectedEntry)), 96.0F);
    ui::KeyValue("Unknown 0", FormatBytes(entry.unknown0), 96.0F);
    ui::KeyValue("Unknown 1", FormatBytes(entry.unknown1), 96.0F);
    ui::VerticalSpace(6.0F);

    ui::PushFont(ui::FontRole::Strong);
    ImGui::TextUnformatted("Open As");
    ui::PopFont();
    int selectedFormat = state.selectedFormat ? static_cast<int>(*state.selectedFormat) + 1 : 0;
    ImGui::SetNextItemWidth(-1.0F);
    if (ImGui::Combo("##open-as", &selectedFormat, kOpenFormatNames.data(), static_cast<int>(kOpenFormatNames.size())))
    {
        ddsViewer.Clear();
        CloseOpenedArchiveEntry(state);
        state.selectedFormat = selectedFormat == 0 ? std::nullopt : std::optional(static_cast<FpkEntryFormat>(selectedFormat - 1));
    }
    const auto width = ImGui::GetContentRegionAvail().x;
    if (ui::PrimaryButton(ui::icon::Eye, "Open Selected in Memory", state.selectedFormat.has_value(), "Choose a parser first. The header hint never chooses one.", width))
    {
        ddsViewer.Clear();
        OpenSelectedArchiveEntryInMemory(*document, state);
    }
    if (ui::SecondaryButton(ui::icon::Download, "Extract Selected...", true, {}, width))
    {
        state.extractionResult.clear();
        ImGui::OpenPopup(kExtractSheet);
    }

    if (!state.openError.empty())
    {
        ui::TonedText(ui::Tone::Error, state.openError);
    }
    if (state.openedDocument || !state.openError.empty())
    {
        auto status = DescribeEmbeddedDocument(state);
        if (!state.presentationError.empty())
        {
            status = { DocumentStage::PresentationFailure, "Parsed. Preview presentation failed", state.presentationError };
        }
        ui::Divider();
        DrawDocumentContext(entryRange, status);
        if (InspectFpkEntryKind(*document, *state.selectedEntry))
        {
            const auto span = std::span<const std::byte>(document->bytes).subspan(entry.offset, entry.size);
            ui::KeyValue("Header bytes", FormatBytes(span.first(std::min(span.size(), kInspectionHeaderSize))), 96.0F);
        }
    }
    if (!state.extractionResult.empty())
    {
        ui::Caption(state.extractionResult);
    }
    ImGui::PushID(std::to_string(*state.selectedEntry).c_str());
    ImGui::PushID(std::to_string(state.openAttempts).c_str());
    drawOpenedInspector(state, ddsViewer);
    ImGui::PopID();
    ImGui::PopID();
    drawExtractionPopup(*document, state);
}

void DrawArchiveWorkArea(const std::filesystem::path& archivePath,
                         const FpkDocument&           document,
                         ArchiveExplorerState&        state,
                         DdsViewer&                   ddsViewer)
{
    const auto archiveName = PathToUtf8(archivePath.filename());
    ui::PushFont(ui::FontRole::Strong);
    ImGui::TextUnformatted(archiveName.c_str());
    ui::PopFont();
    ImGui::SetItemTooltip("%s", PathToUtf8(archivePath).c_str());
    ImGui::SameLine();
    ui::PushFont(ui::FontRole::Caption);
    ImGui::PushStyleColor(ImGuiCol_Text, ui::color::Muted);
    ImGui::TextUnformatted(state.selectedEntry ? ("/  Entry " + std::to_string(*state.selectedEntry + 1)).c_str() : "/  No entry selected");
    ImGui::PopStyleColor();
    ui::PopFont();

    const auto select = [&](std::uint64_t number)
    {
        const auto count = std::min(document.index.entries.size(), kMaximumArchiveNavigationEntries);
        if (number > 0 && number <= count && state.selectedEntry != number - 1)
        {
            ddsViewer.Clear();
        }
        return SelectArchiveEntry(state, count, number);
    };
    const auto entryCount            = document.index.entries.size();
    const auto navigationEntryCount  = std::min(entryCount, kMaximumArchiveNavigationEntries);
    bool       scrollToSelectedEntry = false;

    const float rowWidth   = ImGui::GetContentRegionAvail().x;
    const float jumpWidth  = 90.0F + 8.0F + 104.0F + 8.0F + 32.0F * 2.0F + 4.0F;
    const auto  countLabel = std::to_string(state.visibleEntries.size()) + " of " + std::to_string(document.index.entries.size());
    ui::PushFont(ui::FontRole::Caption);
    const float chipWidth = ImGui::CalcTextSize(countLabel.c_str()).x + 40.0F;
    ui::PopFont();
    const bool  stack      = rowWidth < jumpWidth + chipWidth + 220.0F;
    const float filterWide = std::max(40.0F, stack ? rowWidth - chipWidth - 8.0F : std::min(360.0F, rowWidth - jumpWidth - chipWidth - 24.0F));
    ImGui::SetNextItemWidth(filterWide);
    ImGui::InputTextWithHint("##filter-metadata", "Filter metadata", state.filter.data(), state.filter.size());
    ImGui::SetItemTooltip("Entry number, header hint, decimal or hex range, or unknown bytes.");
    UpdateArchiveFilter(document, state);
    ImGui::SameLine();
    ui::Chip(state.visibleEntries.size() == document.index.entries.size() ? ui::Tone::Neutral : ui::Tone::Accent,
             countLabel,
             "Matching entries / retained entries",
             ui::icon::ListChecks);
    if (!stack)
    {
        ImGui::SameLine(ImGui::GetCursorPosX() + rowWidth - jumpWidth);
    }
    ImGui::BeginDisabled(navigationEntryCount == 0);
    ImGui::SetNextItemWidth(90.0F);
    const bool jumpSubmitted = ImGui::InputScalar("##entry-number", ImGuiDataType_U64, &state.requestedEntry, nullptr, nullptr, nullptr, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SetItemTooltip("Entry number");
    ImGui::SameLine();
    const bool jumpClicked = ui::SecondaryButton(nullptr, "Go to entry", true, {}, 104.0F);
    ImGui::EndDisabled();
    if (jumpSubmitted || jumpClicked)
    {
        scrollToSelectedEntry = select(state.requestedEntry);
    }
    ImGui::SameLine();
    const auto previous = FindArchiveNeighbor(state, false);
    if (ui::IconButton("previous", ui::icon::ChevronLeft, "Previous", false, previous.has_value()))
    {
        scrollToSelectedEntry = select(*previous + 1);
    }
    ImGui::SameLine(0.0F, 4.0F);
    const auto next = FindArchiveNeighbor(state, true);
    if (ui::IconButton("next", ui::icon::ChevronRight, "Next", false, next.has_value()))
    {
        scrollToSelectedEntry = select(*next + 1);
    }

    if (entryCount > navigationEntryCount)
    {
        ui::TonedText(ui::Tone::Warn,
                      "This archive exceeds the entry list limit. Only entries 1 through " +
                          std::to_string(navigationEntryCount) + " can be selected.");
    }
    if (!state.navigationError.empty())
    {
        ui::TonedText(ui::Tone::Error, state.navigationError);
    }

    const auto selectedRow      = state.selectedEntry ? std::lower_bound(state.visibleEntries.begin(), state.visibleEntries.end(), *state.selectedEntry) : state.visibleEntries.end();
    const bool selectionVisible = state.selectedEntry && selectedRow != state.visibleEntries.end() && *selectedRow == *state.selectedEntry;
    if (state.selectedEntry && !selectionVisible)
    {
        ui::TonedText(ui::Tone::Warn, "Selected entry " + std::to_string(*state.selectedEntry + 1) + " is hidden by the filter. Inspector and explicit actions still address that entry.");
    }
    if (state.visibleEntries.empty())
    {
        ui::Muted("No entries match the filter.");
    }
    constexpr auto flags = ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX | ImGuiTableFlags_Resizable;
    const auto&    style = ImGui::GetStyle();
    if (ImGui::BeginTable("##fpk-entries", 3, flags, ImVec2(-1.0F, std::clamp(ImGui::GetContentRegionAvail().y * 0.4F, 150.0F, 260.0F))))
    {
        ImGui::TableSetupColumn("Entry", ImGuiTableColumnFlags_WidthFixed, 80.0F);
        ImGui::TableSetupColumn("Header hint", ImGuiTableColumnFlags_WidthFixed, 140.0F);
        const auto rangeWidth = ImGui::CalcTextSize("4294967295 (0xFFFFFFFF) + 4294967295 (0xFFFFFFFF) = 4294967295 (0xFFFFFFFF)").x + style.CellPadding.x * 2.0F;
        ImGui::TableSetupColumn("Byte range (exclusive end)", ImGuiTableColumnFlags_WidthFixed, rangeWidth);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(state.visibleEntries.size()));
        if (scrollToSelectedEntry && selectionVisible)
        {
            clipper.IncludeItemByIndex(static_cast<int>(selectedRow - state.visibleEntries.begin()));
        }
        while (clipper.Step())
        {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
            {
                const auto  index = state.visibleEntries[static_cast<std::size_t>(row)];
                const auto& entry = document.index.entries[index];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                const auto label    = "Entry " + std::to_string(index + 1);
                const bool selected = state.selectedEntry == index;
                if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns))
                {
                    (void)select(index + 1);
                }
                if (scrollToSelectedEntry && selected)
                {
                    ImGui::SetScrollHereY(0.5F);
                    scrollToSelectedEntry = false;
                }
                ImGui::TableNextColumn();
                const auto hint = ArchiveEntryHintName(state.entryHints[index]);
                ImGui::PushStyleColor(ImGuiCol_Text, state.entryHints[index] == FpkEntryKind::Unknown ? ui::color::Faint : ui::color::Text);
                ImGui::TextUnformatted(hint.data(), hint.data() + hint.size());
                ImGui::PopStyleColor();
                ImGui::TableNextColumn();
                ImGui::Text("%u (0x%08X) + %u (0x%08X) = %llu (0x%08llX)",
                            entry.offset,
                            entry.offset,
                            entry.size,
                            entry.size,
                            static_cast<unsigned long long>(entry.offset) + entry.size,
                            static_cast<unsigned long long>(entry.offset) + entry.size);
            }
        }
        ImGui::EndTable();
    }
}

void DrawArchivePreview(const std::filesystem::path& archivePath,
                        ArchiveExplorerState&        state,
                        DdsViewer&                   ddsViewer)
{
    if (state.nifPreview)
    {
        const NifDocument* nif = nullptr;
        if (state.openedDocument)
        {
            nif = std::get_if<NifDocument>(&state.openedDocument->data);
        }
        ddsViewer.Clear();
        DrawNifPreview(nif,
                       state.nifPreview->model ? &*state.nifPreview->model : nullptr,
                       state.nifPreview->assemblyError,
                       state.openError,
                       state.nifPreview->navigation);
        return;
    }

    if (!state.openedDocument)
    {
        ddsViewer.Clear();
        const auto height = std::max(160.0F, ImGui::GetContentRegionAvail().y);
        if (state.openError.empty())
        {
            ui::EmptyState("entry-preview", ui::icon::Eye, "Nothing Opened", "Choose a parser under Open As in the details panel, then open the selected entry in memory.", nullptr, nullptr, height);
        }
        else
        {
            ui::EmptyState("entry-preview", ui::icon::XCircle, "This entry could not be opened", state.openError, nullptr, nullptr, height);
        }
        return;
    }

    const auto& data = state.openedDocument->data;
    if (const auto* dds = std::get_if<DdsDocument>(&data))
    {
        const auto sourceId = archivePath.string() + "#entry-" +
                              std::to_string(state.openedDocument->entryIndex + 1);
        ddsViewer.Draw(dds, sourceId, {});
    }
    else if (const auto* mp3 = std::get_if<Mp3Document>(&data))
    {
        ddsViewer.Clear();
        DrawMp3Preview(mp3, {});
    }
    else if (const auto* map = std::get_if<MapDocument>(&data))
    {
        ddsViewer.Clear();
        DrawMapPreview(map, {});
    }
    else
    {
        ddsViewer.Clear();
        ui::EmptyState("entry-inspector", ui::icon::Info, "No Visual Preview", "The opened entry's details are in the Inspector.", nullptr, nullptr, std::max(160.0F, ImGui::GetContentRegionAvail().y));
    }
}

} // namespace rerevved::tools
