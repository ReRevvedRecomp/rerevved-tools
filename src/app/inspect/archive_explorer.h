#pragma once

#include "fpk_document.h"
#include "inspect/dds_viewer.h"
#include "inspect/nif_inspector.h"
#include "inspect/nif_preview.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rerevved::tools
{

// ImGuiListClipper reserves INT_MAX for an unknown item count.
inline constexpr std::size_t kMaximumArchiveNavigationEntries =
    static_cast<std::size_t>(std::numeric_limits<int>::max()) - 1;

struct ExtractionModalLayout
{
    float contentWidth;
    float buttonWidth;
    bool  stackButtons;
};

[[nodiscard]] constexpr ExtractionModalLayout CalculateExtractionModalLayout(
    float workWidth,
    float horizontalWindowPadding,
    float itemSpacing)
{
    const auto contentWidth =
        std::max(1.0F,
                 std::min(600.0F, workWidth - horizontalWindowPadding * 2.0F));
    const auto pairedButtonWidth =
        std::min(120.0F, std::max(1.0F, (contentWidth - itemSpacing) / 2.0F));
    const bool stackButtons = pairedButtonWidth < 120.0F;
    const auto buttonWidth =
        stackButtons ? std::min(120.0F, contentWidth) : pairedButtonWidth;
    return { contentWidth, buttonWidth, stackButtons };
}

[[nodiscard]] constexpr float CalculateArchiveLabeledControlWidth(
    float availableWidth,
    float preferredWidth,
    float labelWidth,
    float labelSpacing)
{
    return std::min(
        preferredWidth,
        std::max(1.0F, availableWidth - labelWidth - labelSpacing));
}

struct ArchiveActionRowLayout
{
    float firstWidth;
    float secondWidth;
    bool  sameLine;
};

[[nodiscard]] constexpr ArchiveActionRowLayout CalculateArchiveActionRowLayout(
    float availableWidth,
    float preferredFirstWidth,
    float firstLabelWidth,
    float preferredSecondWidth,
    float itemSpacing,
    float labelSpacing)
{
    const auto firstWidth = CalculateArchiveLabeledControlWidth(
        availableWidth, preferredFirstWidth, firstLabelWidth, labelSpacing);
    const auto secondWidth =
        std::min(preferredSecondWidth, std::max(1.0F, availableWidth));
    const auto preferredRowWidth = preferredFirstWidth + firstLabelWidth +
                                   labelSpacing + itemSpacing + preferredSecondWidth;
    return { firstWidth, secondWidth, preferredRowWidth <= availableWidth };
}

struct ArchiveExplorerState
{
    std::optional<std::size_t>      selectedEntry;
    std::optional<FpkEntryFormat>   selectedFormat;
    std::optional<FpkEntryDocument> openedDocument;
    std::string                     openError;
    std::uint64_t                   openAttempts = 0;
    std::string                     presentationError;
    std::array<char, 2048>          extractionPath{};
    std::string                     extractionResult;
    std::string                     metadataResult;
    std::uint64_t                   requestedEntry = 1;
    std::string                     navigationError;
    std::array<char, 256>           filter{};
    std::string                     appliedFilter;
    bool                            filterInitialized = false;
    std::vector<FpkEntryKind>       entryHints;
    std::vector<std::size_t>        visibleEntries;
    NifInspectorState               nifInspector;
    InspectorNavigationState        gfxInspector;
    InspectorNavigationState        mapInspector;

    struct NifPreview
    {
        std::optional<NifModel>      model;
        std::optional<NifModelError> assemblyError;
        NifPreviewState              navigation;
    };

    std::optional<NifPreview> nifPreview;
};

inline void CloseOpenedArchiveEntry(ArchiveExplorerState& state)
{
    state.nifInspector = {};
    state.gfxInspector = {};
    state.mapInspector = {};
    state.openedDocument.reset();
    state.nifPreview.reset();
    state.openError.clear();
    state.presentationError.clear();
}

inline void OpenSelectedArchiveEntryInMemory(const FpkDocument&    document,
                                             ArchiveExplorerState& state)
{
    if (!state.selectedEntry || !state.selectedFormat)
    {
        return;
    }
    ++state.openAttempts;
    state.nifInspector = {};
    state.gfxInspector = {};
    state.mapInspector = {};
    state.presentationError.clear();
    state.nifPreview.reset();
    auto opened = OpenFpkEntryDocument(
        document, *state.selectedEntry, *state.selectedFormat);
    if (!opened)
    {
        state.openedDocument.reset();
        state.openError = std::move(opened.error());
        if (state.selectedFormat == FpkEntryFormat::Nif)
        {
            state.nifPreview.emplace();
        }
        return;
    }

    state.openedDocument = std::move(*opened);
    state.openError.clear();
    const auto* nif = std::get_if<NifDocument>(&state.openedDocument->data);
    if (!nif)
    {
        return;
    }

    auto& preview = state.nifPreview.emplace();
    auto  model   = AssembleNifModel(*nif);
    if (model)
    {
        preview.model = std::move(*model);
    }
    else
    {
        preview.assemblyError = model.error();
    }
}

[[nodiscard]] inline bool SelectArchiveEntry(ArchiveExplorerState& state,
                                             std::size_t           entryCount,
                                             std::uint64_t         oneBasedEntry)
{
    entryCount = std::min(entryCount, kMaximumArchiveNavigationEntries);
    if (entryCount == 0)
    {
        state.navigationError = "This FPK archive has no entries.";
        return false;
    }
    if (oneBasedEntry == 0 || oneBasedEntry > entryCount)
    {
        state.navigationError =
            "Enter an entry number from 1 to " + std::to_string(entryCount) + ".";
        return false;
    }

    const auto selectedEntry = static_cast<std::size_t>(oneBasedEntry - 1);
    state.requestedEntry     = oneBasedEntry;
    state.navigationError.clear();
    if (state.selectedEntry && *state.selectedEntry == selectedEntry)
    {
        return true;
    }

    state.selectedEntry = selectedEntry;
    state.selectedFormat.reset();
    state.nifInspector = {};
    state.gfxInspector = {};
    state.mapInspector = {};
    state.openedDocument.reset();
    state.nifPreview.reset();
    state.openError.clear();
    state.extractionResult.clear();
    state.presentationError.clear();
    state.metadataResult.clear();
    return true;
}

inline void SelectInitialArchiveEntry(ArchiveExplorerState& state,
                                      const FpkDocument&    document)
{
    if (state.selectedEntry || document.index.entries.empty())
    {
        return;
    }
    (void)SelectArchiveEntry(state, document.index.entries.size(), 1);
}

void DrawArchiveInspector(const FpkDocument*    document,
                          ArchiveExplorerState& state,
                          std::string_view      error,
                          DdsViewer&            ddsViewer);

[[nodiscard]] std::string_view           ArchiveEntryHintName(FpkEntryKind kind);
void                                     UpdateArchiveFilter(const FpkDocument& document, ArchiveExplorerState& state);
[[nodiscard]] std::optional<std::size_t> FindArchiveNeighbor(
    const ArchiveExplorerState& state, bool next);
void DrawArchiveWorkArea(const std::filesystem::path& archivePath,
                         const FpkDocument&           document,
                         ArchiveExplorerState&        state,
                         DdsViewer&                   ddsViewer);

[[nodiscard]] std::string_view ArchiveFormatName(FpkEntryFormat format);

void DrawArchivePreview(const std::filesystem::path& archivePath,
                        ArchiveExplorerState&        state,
                        DdsViewer&                   ddsViewer);

} // namespace rerevved::tools
