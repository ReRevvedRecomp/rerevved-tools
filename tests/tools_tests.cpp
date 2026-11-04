#include "dds_document.h"
#include "dds_metadata.h"
#include "file_inspection.h"
#include "file_kind.h"
#include "fpk_document.h"
#include "fpk_extraction.h"
#include "fpk_index.h"
#include "gfx_document.h"
#include "inspect/archive_explorer.h"
#include "inspect/asset_inspector_format.h"
#include "inspect/document_workspace.h"
#include "inspect/inspection_format.h"
#include "inspect/inspector_navigation.h"
#include "inspect/nif_inspector.h"
#include "inspect/nif_inspector_format.h"
#include "inspect/nif_preview.h"
#include "map_document.h"
#include "mp3_document.h"
#include "nif_document.h"
#include "nif_model.h"
#include "synthetic_mp3.h"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace
{

int failures = 0;

std::vector<std::byte> makeNavigationGfx();

void expect(bool condition, std::string_view message)
{
    if (condition)
    {
        return;
    }
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

void testCreatorActivityKinds()
{
    using namespace rerevved::tools;
    DocumentWorkspace workspace;
    workspace.RecordCreatorActivity(OpenOutcomeKind::ExportPublished, "published-pack", "Exported");
    const auto& published = workspace.Activity().back();
    expect(published.origin == OpenOrigin::Creator && OpenOriginName(published.origin) == "Creator", "export Activity identifies its creator origin");
    expect(published.status.stage == DocumentStage::Ready && published.status.error.empty(), "a published export is Ready");
    expect(OpenOutcomeName(OpenOutcomeKind::ExportPublished) != OpenOutcomeName(OpenOutcomeKind::ExportFailed), "export outcomes have distinct Activity labels");
    workspace.RecordCreatorActivity(OpenOutcomeKind::ExportFailed, "pack", "Export stopped", "exact export diagnostic");
    expect(workspace.Activity().back().status.stage != DocumentStage::Ready && workspace.Activity().back().status.error == "exact export diagnostic", "a failed export retains its diagnostic and never reports Ready");
}

void testFileKinds()
{
    using rerevved::tools::ClassifyFile;
    using rerevved::tools::FileKind;

    expect(ClassifyFile("Common0.FPK") == FileKind::FpkArchive,
           "FPK classification is case-insensitive");
    expect(ClassifyFile("portrait.dds") == FileKind::DdsTexture,
           "DDS classification");
    expect(ClassifyFile("menu.GfX") == FileKind::GfxMovie,
           "GFx classification is case-insensitive");
    expect(ClassifyFile("intro.bik") == FileKind::BikVideo,
           "Bink classification");
    expect(ClassifyFile("music.mp3") == FileKind::Mp3Audio,
           "MP3 classification");
    expect(ClassifyFile("terrain.NIF") == FileKind::NifContainer,
           "NIF classification is case-insensitive");
    expect(ClassifyFile("scenario.MaP") == FileKind::MapRecord,
           "map classification is case-insensitive");
    expect(ClassifyFile("notes.txt") == FileKind::Unknown,
           "unknown extension classification");
}

void testDocumentErrorMessages()
{
    using namespace rerevved::tools;

    expect(DdsDocumentErrorMessage(DdsDocumentError{ DdsMetadataError::Truncated }) ==
               "DDS metadata is truncated.",
           "DDS truncated-metadata message");
    expect(DdsDocumentErrorMessage(DdsDocumentError{ DdsMetadataError::InvalidSignature }) ==
               "The file does not have a valid DDS signature.",
           "DDS invalid-signature message");
    expect(DdsDocumentErrorMessage(DdsDocumentError{ DdsMetadataError::InvalidHeaderSize }) ==
               "The DDS header size is invalid.",
           "DDS invalid-header-size message");
    expect(DdsDocumentErrorMessage(
               DdsDocumentError{ DdsMetadataError::InvalidPixelFormatSize }) ==
               "The DDS pixel format size is invalid.",
           "DDS invalid-pixel-format-size message");
    expect(DdsDocumentErrorMessage(DdsDocumentError{ DdsPreviewError::UnsupportedFormat }) ==
               "This DDS encoding is not supported for preview.",
           "DDS unsupported-preview message");
    expect(DdsDocumentErrorMessage(DdsDocumentError{ DdsPreviewError::InvalidDimensions }) ==
               "The DDS preview dimensions are invalid.",
           "DDS invalid-dimensions message");
    expect(DdsDocumentErrorMessage(DdsDocumentError{ DdsPreviewError::SizeOverflow }) ==
               "The DDS preview dimensions exceed the supported size range.",
           "DDS size-overflow message");
    expect(DdsDocumentErrorMessage(DdsDocumentError{ DdsPreviewError::TruncatedPixelData }) ==
               "The DDS top level pixel data is truncated.",
           "DDS truncated-pixel-data message");

    expect(GfxDocumentErrorMessage(GfxDocumentError::Truncated) ==
               "The GFX movie is truncated.",
           "GFX truncated message");
    expect(GfxDocumentErrorMessage(GfxDocumentError::InvalidSignature) ==
               "The file does not have a valid GFX signature.",
           "GFX invalid-signature message");
    expect(GfxDocumentErrorMessage(GfxDocumentError::UnsupportedCompression) ==
               "Compressed GFX movies are not supported.",
           "GFX unsupported-compression message");
    expect(GfxDocumentErrorMessage(GfxDocumentError::LengthMismatch) ==
               "The GFX declared length does not match the file size.",
           "GFX length-mismatch message");
    expect(GfxDocumentErrorMessage(GfxDocumentError::MissingExporterInfo) ==
               "The GFX movie does not begin with exporter information.",
           "GFX missing-exporter message");
    expect(GfxDocumentErrorMessage(GfxDocumentError::UnsupportedExporterVersion) ==
               "The GFX exporter version is not supported.",
           "GFX unsupported-exporter message");
    expect(GfxDocumentErrorMessage(GfxDocumentError::InvalidTagStream) ==
               "The GFX tag stream is invalid.",
           "GFX invalid-tag-stream message");

    expect(NifDocumentErrorMessage(NifDocumentError::Truncated) ==
               "The NIF file is truncated.",
           "NIF truncated message");
    expect(NifDocumentErrorMessage(NifDocumentError::InvalidSignature) ==
               "The file does not have a valid Gamebryo NIF signature.",
           "NIF invalid-signature message");
    expect(NifDocumentErrorMessage(NifDocumentError::UnsupportedVersion) ==
               "The NIF file version is not supported.",
           "NIF unsupported-version message");
    expect(NifDocumentErrorMessage(NifDocumentError::UnsupportedEndian) ==
               "Only the observed big endian NIF layout is supported.",
           "NIF unsupported-endian message");
    expect(NifDocumentErrorMessage(NifDocumentError::UnsupportedUserVersion) ==
               "The NIF user version is not supported.",
           "NIF unsupported-user-version message");
    expect(NifDocumentErrorMessage(NifDocumentError::InvalidLayout) ==
               "The NIF header, block table, or footer is inconsistent.",
           "NIF invalid-layout message");

    expect(Mp3DocumentErrorMessage(Mp3DocumentError::DecodeFailed) ==
               "The MP3 audio could not be decoded.",
           "MP3 decode-failure message");
    expect(MapDocumentErrorMessage(MapDocumentError::Truncated) ==
               "The map record is truncated. The Xbox DLC profile requires 1088 bytes.",
           "MAP truncated message");
    expect(MapDocumentErrorMessage(MapDocumentError::UnsupportedLength) ==
               "The map record length is unsupported. The Xbox DLC profile requires exactly 1088 bytes.",
           "MAP unsupported-length message");
}

void testInspectorNavigation()
{
    using namespace rerevved::tools;

    const std::vector<std::string> labels{
        "Block 0: NiNode",
        "Block 1: NiTriShape",
        "String 2: Texture\\x00.dds",
    };
    const auto matching = FilterInspectorIndices(labels, "TRISHAPE");
    expect(matching == std::vector<std::size_t>{ 1 },
           "inspector filtering is ASCII case-insensitive and preserves original indices");
    expect(FilterInspectorIndices(labels, "missing").empty(),
           "inspector filtering reports an empty no-match result");

    InspectorNavigationState state;
    expect(SelectInspectorIndex(state, 2, labels.size()) && state.selectedOriginalIndex == 2,
           "inspector selection keeps the source-order original index");
    const auto hidden = FilterInspectorIndices(labels, "NiNode");
    expect(InspectorSelectionHidden(state, hidden),
           "a selected record reports hidden status when filtering removes it");
    expect(FindInspectorNeighbor(hidden, state.selectedOriginalIndex, false) == 0 &&
               !FindInspectorNeighbor(hidden, state.selectedOriginalIndex, true),
           "previous and next follow filtered source order and stop at boundaries");

    expect(!SelectInspectorIndex(state, labels.size(), labels.size()) &&
               state.selectedOriginalIndex == 2,
           "an out-of-range selection preserves the prior selection");
    expect(!JumpInspectorIndex(state, "99", 0, labels.size()) &&
               state.selectedOriginalIndex == 2,
           "an invalid zero-based original-index jump preserves selection");
    expect(JumpInspectorIndex(state, "2", 1, labels.size()) &&
               state.selectedOriginalIndex == 1,
           "a one-based jump converts to the matching zero-based source index");
    expect(!JumpInspectorIndex(state, "0", 1, labels.size()) &&
               state.selectedOriginalIndex == 1,
           "a one-based jump rejects zero without changing selection");
    expect(JumpInspectorIndex(state, "2", 0, labels.size()) &&
               state.selectedOriginalIndex == 2 &&
               InspectorSelectionHidden(state, hidden),
           "a valid jump may retain a record hidden by the active filter");
    expect(!JumpInspectorIndex(state,
                               "184467440737095516160",
                               0,
                               labels.size()) &&
               state.selectedOriginalIndex == 2,
           "an overflowing original-index jump preserves selection");
    expect(!FindInspectorNeighbor({}, state.selectedOriginalIndex, false) &&
               !FindInspectorNeighbor({}, state.selectedOriginalIndex, true),
           "previous and next return no match for an empty filtered inventory");

    InspectorNavigationState direct;
    InspectorNavigationState embedded;
    const auto               directMatching   = FilterInspectorIndices(labels, "block");
    const auto               embeddedMatching = FilterInspectorIndices(labels, "block");
    expect(directMatching == embeddedMatching &&
               SelectInspectorIndex(direct, 1, labels.size()) &&
               SelectInspectorIndex(embedded, 1, labels.size()) &&
               direct.selectedOriginalIndex == embedded.selectedOriginalIndex,
           "direct and embedded retained inventories share presentation identity semantics");
    direct.filter[0]       = 'x';
    direct.filter[1]       = '\0';
    direct.navigationError = "stale";
    ResetInspectorNavigationState(direct);
    expect(!direct.selectedOriginalIndex && direct.filter[0] == '\0' &&
               direct.navigationError.empty() && embedded.selectedOriginalIndex == 1,
           "navigation state resets independently for each owning document");

    NifInspectorState directNif;
    NifInspectorState embeddedNif;
    directNif.currentSection                         = NifInspectorSection::SourceTextures;
    directNif.sourceTextures.selectedOriginalIndex   = 2;
    embeddedNif.sourceTextures.selectedOriginalIndex = 1;
    ResetNifInspectorState(directNif);
    expect(directNif.currentSection == NifInspectorSection::BlockTypes &&
               !directNif.sourceTextures.selectedOriginalIndex &&
               embeddedNif.sourceTextures.selectedOriginalIndex == 1,
           "direct and embedded NIF section state resets independently");
}

void testRetainedAssetInspectorPresentation()
{
    using namespace rerevved::tools;

    DdsDocument dds;
    dds.metadata.reserved1[0]       = std::byte{ 0xA5 };
    dds.metadata.pixelFormat.fourCc = {
        std::byte{ 'D' }, std::byte{ 'X' }, std::byte{ '1' }, std::byte{ '0' }
    };
    const auto ddsFields = FormatDdsInspection(dds);
    expect(ddsFields.size() > 7 && ddsFields[5].starts_with("Reserved 1: A5") &&
               ddsFields[7] == "FourCC bytes: 44 58 31 30",
           "DDS Inspector retains reserved and FourCC bytes in escaped raw form");
    const FpkEntryDocument embeddedDds{ 3, FpkEntryFormat::Dds, dds };
    const auto*            embeddedDdsDocument = std::get_if<DdsDocument>(&embeddedDds.data);
    expect(embeddedDdsDocument && FormatDdsInspection(*embeddedDdsDocument) == ddsFields,
           "direct and embedded DDS summaries are identical");

    GfxDocument gfx;
    gfx.externalImages.resize(2);
    gfx.externalImages[0].fileName = { std::byte{ 'a' }, std::byte{ '.' }, std::byte{ 'd' } };
    gfx.externalImages[1].fileName = { std::byte{ 'b' }, std::byte{ '.' }, std::byte{ 'd' } };
    const FpkEntryDocument   embeddedGfx{ 4, FpkEntryFormat::Gfx, gfx };
    const auto*              embeddedGfxDocument = std::get_if<GfxDocument>(&embeddedGfx.data);
    InspectorNavigationState directGfxState;
    InspectorNavigationState embeddedGfxState;
    expect(embeddedGfxDocument &&
               JumpInspectorIndex(directGfxState, "2", 1, gfx.externalImages.size()) &&
               JumpInspectorIndex(embeddedGfxState,
                                  "2",
                                  1,
                                  embeddedGfxDocument->externalImages.size()) &&
               directGfxState.selectedOriginalIndex == embeddedGfxState.selectedOriginalIndex &&
               directGfxState.selectedOriginalIndex == 1,
           "direct and embedded GFX resource ordinals share one-based navigation identity");
    const auto navigationGfx = ParseGfxDocument(makeNavigationGfx());
    expect(navigationGfx && navigationGfx->externalImages.size() == 2 &&
               FormatGfxBytes(navigationGfx->externalImages[0].exportName).find("##") !=
                   std::string::npos &&
               FormatGfxBytes(navigationGfx->externalImages[0].fileName).size() > 100 &&
               navigationGfx->externalImages[0].fileName ==
                   navigationGfx->externalImages[1].fileName,
           "GFX navigation fixture retains duplicate and long escaped external names");

    Mp3Document mp3;
    mp3.sampleRate                   = 44100;
    mp3.channels                     = 2;
    mp3.previewFrameCount            = 17;
    mp3.previewComplete              = false;
    mp3.waveform                     = { -1.0F, 0.0F, 1.0F };
    const auto             mp3Fields = FormatMp3Inspection(mp3);
    const FpkEntryDocument embeddedMp3{ 4, FpkEntryFormat::Mp3, mp3 };
    const auto*            embeddedMp3Document = std::get_if<Mp3Document>(&embeddedMp3.data);
    expect(embeddedMp3Document && FormatMp3Inspection(*embeddedMp3Document) == mp3Fields,
           "direct and embedded MP3 summaries are identical");

    MapDocument map{};
    map.core[0]                      = std::byte{ 0x01 };
    map.footer[0]                    = std::byte{ 0xFE };
    const auto             mapFields = FormatMapInspection(map);
    const auto             mapRows   = FormatMapByteRows(map);
    const FpkEntryDocument embeddedMap{ 5, FpkEntryFormat::Map, map };
    const auto*            embeddedMapDocument = std::get_if<MapDocument>(&embeddedMap.data);
    expect(embeddedMapDocument && FormatMapInspection(*embeddedMapDocument) == mapFields &&
               FormatMapByteRows(*embeddedMapDocument) == mapRows &&
               mapRows.front().find("Offset 0") != std::string::npos &&
               mapRows.back().find("Offset 1087") != std::string::npos,
           "direct and embedded MAP summaries and byte rows preserve file order");
}

void testArchiveNavigation()
{
    using rerevved::tools::ArchiveExplorerState;
    using rerevved::tools::kMaximumArchiveNavigationEntries;
    using rerevved::tools::SelectArchiveEntry;

    ArchiveExplorerState state;
    state.selectedFormat = rerevved::tools::FpkEntryFormat::Nif;
    state.nifPreview.emplace();
    state.openedDocument.emplace();
    state.openError        = "old open error";
    state.extractionResult = "old extraction result";
    state.metadataResult   = "old metadata result";
    state.navigationError  = "old navigation error";
    expect(SelectArchiveEntry(state, 5, 3), "valid archive entry selection succeeds");
    expect(state.selectedEntry && *state.selectedEntry == 2 && state.requestedEntry == 3,
           "archive selection stores matching zero-based and displayed entry numbers");
    expect(!state.selectedFormat && !state.nifPreview && !state.openedDocument && state.openError.empty() &&
               state.extractionResult.empty() && state.metadataResult.empty() &&
               state.navigationError.empty(),
           "changed archive selection clears entry-dependent results");

    state.openedDocument.emplace();
    state.openError        = "current open error";
    state.extractionResult = "current extraction result";
    state.metadataResult   = "current metadata result";
    state.navigationError  = "old navigation error";
    expect(SelectArchiveEntry(state, 5, 3), "current archive entry can be selected again");
    expect(state.openedDocument && state.openError == "current open error" &&
               state.extractionResult == "current extraction result" &&
               state.metadataResult == "current metadata result" &&
               state.navigationError.empty(),
           "reselecting the current archive entry preserves its results");

    state.requestedEntry = 0;
    expect(!SelectArchiveEntry(state, 5, state.requestedEntry),
           "zero archive entry number is rejected");
    expect(state.selectedEntry && *state.selectedEntry == 2 && state.openedDocument &&
               state.metadataResult == "current metadata result" &&
               state.navigationError == "Enter an entry number from 1 to 5.",
           "invalid archive jump preserves the current selection and reports its range");

    state.requestedEntry = 6;
    expect(!SelectArchiveEntry(state, 5, state.requestedEntry),
           "archive entry number beyond the index is rejected");
    expect(state.selectedEntry && *state.selectedEntry == 2 && state.openedDocument &&
               state.navigationError == "Enter an entry number from 1 to 5.",
           "out-of-range archive jump preserves the current selection");

    expect(!SelectArchiveEntry(state, 0, 1), "empty archive entry selection is rejected");
    expect(state.selectedEntry && *state.selectedEntry == 2 && state.openedDocument &&
               state.navigationError == "This FPK archive has no entries.",
           "empty archive navigation preserves the current selection and reports its state");

    ArchiveExplorerState boundaryState;
    expect(SelectArchiveEntry(boundaryState, 5, 2) && boundaryState.selectedEntry == 1,
           "archive navigation can select the second entry");
    expect(SelectArchiveEntry(boundaryState, 5, *boundaryState.selectedEntry) &&
               boundaryState.selectedEntry == 0,
           "previous navigation reaches the first entry");
    expect(SelectArchiveEntry(boundaryState, 5, 4) && boundaryState.selectedEntry == 3,
           "archive navigation can select the penultimate entry");
    expect(SelectArchiveEntry(boundaryState, 5, *boundaryState.selectedEntry + 2) &&
               boundaryState.selectedEntry == 4,
           "next navigation reaches the final entry");

    boundaryState.requestedEntry = std::numeric_limits<std::uint64_t>::max();
    expect(!SelectArchiveEntry(boundaryState, 5, boundaryState.requestedEntry) &&
               boundaryState.selectedEntry == 4 &&
               boundaryState.navigationError == "Enter an entry number from 1 to 5.",
           "maximum unsigned archive jump is rejected without changing selection");

    const auto firstUnlistedEntry =
        static_cast<std::uint64_t>(kMaximumArchiveNavigationEntries) + 1;
    expect(kMaximumArchiveNavigationEntries ==
               static_cast<std::size_t>(std::numeric_limits<int>::max()) - 1,
           "archive navigation reserves the ImGui unknown-count sentinel");
    expect(!SelectArchiveEntry(
               boundaryState, kMaximumArchiveNavigationEntries, firstUnlistedEntry) &&
               boundaryState.selectedEntry == 4,
           "entry beyond the signed ImGui list limit is rejected without changing selection");
}

void testExtractionModalLayout()
{
    using rerevved::tools::CalculateExtractionModalLayout;

    const auto wide = CalculateExtractionModalLayout(1280.0F, 8.0F, 8.0F);
    expect(wide.contentWidth == 600.0F && wide.buttonWidth == 120.0F &&
               !wide.stackButtons,
           "wide extraction modal uses preferred content and button widths");

    const auto exactPair = CalculateExtractionModalLayout(264.0F, 8.0F, 8.0F);
    expect(exactPair.contentWidth == 248.0F && exactPair.buttonWidth == 120.0F &&
               !exactPair.stackButtons,
           "extraction buttons remain paired when both preferred widths fit");

    const auto narrow = CalculateExtractionModalLayout(263.0F, 8.0F, 8.0F);
    expect(narrow.contentWidth == 247.0F && narrow.buttonWidth == 120.0F &&
               narrow.stackButtons,
           "extraction buttons stack before their preferred widths would shrink");

    const auto small = CalculateExtractionModalLayout(96.0F, 8.0F, 8.0F);
    expect(small.contentWidth == 80.0F && small.buttonWidth == 80.0F &&
               small.stackButtons,
           "small extraction modal keeps stacked buttons within the work area");

    const auto minimal = CalculateExtractionModalLayout(8.0F, 8.0F, 8.0F);
    expect(minimal.contentWidth == 1.0F && minimal.buttonWidth == 1.0F &&
               minimal.stackButtons,
           "minimal extraction modal retains reachable positive-width controls");
}

void testArchiveActionRowLayout()
{
    using rerevved::tools::CalculateArchiveActionRowLayout;
    using rerevved::tools::CalculateArchiveLabeledControlWidth;

    const auto wideEntry =
        CalculateArchiveActionRowLayout(500.0F, 140.0F, 80.0F, 90.0F, 8.0F, 4.0F);
    expect(wideEntry.firstWidth == 140.0F && wideEntry.secondWidth == 90.0F &&
               wideEntry.sameLine,
           "wide archive entry row retains preferred input and natural action widths");

    const auto exactEntry =
        CalculateArchiveActionRowLayout(322.0F, 140.0F, 80.0F, 90.0F, 8.0F, 4.0F);
    expect(exactEntry.firstWidth == 140.0F && exactEntry.secondWidth == 90.0F &&
               exactEntry.sameLine,
           "exact-fit archive entry row remains on one line");

    const auto narrowEntry =
        CalculateArchiveActionRowLayout(321.0F, 140.0F, 80.0F, 90.0F, 8.0F, 4.0F);
    expect(narrowEntry.firstWidth == 140.0F && narrowEntry.secondWidth == 90.0F &&
               !narrowEntry.sameLine,
           "archive entry action stacks just below the preferred fit");

    const auto minimalEntry =
        CalculateArchiveActionRowLayout(0.0F, 140.0F, 80.0F, 90.0F, 8.0F, 4.0F);
    expect(minimalEntry.firstWidth == 1.0F && minimalEntry.secondWidth == 1.0F &&
               !minimalEntry.sameLine,
           "minimal archive entry row retains positive-width controls");

    const auto exactNavigation =
        CalculateArchiveActionRowLayout(128.0F, 70.0F, 0.0F, 50.0F, 8.0F, 0.0F);
    const auto narrowNavigation =
        CalculateArchiveActionRowLayout(127.0F, 70.0F, 0.0F, 50.0F, 8.0F, 0.0F);
    expect(exactNavigation.sameLine && !narrowNavigation.sameLine,
           "previous and next retain exact-fit pairing and stack before clipping");

    expect(CalculateArchiveLabeledControlWidth(304.0F, 220.0F, 80.0F, 4.0F) ==
                   220.0F &&
               CalculateArchiveLabeledControlWidth(303.0F, 220.0F, 80.0F, 4.0F) ==
                   219.0F &&
               CalculateArchiveLabeledControlWidth(0.0F, 220.0F, 80.0F, 4.0F) == 1.0F,
           "archive format selector prefers 220 pixels and stays positive when narrow");

    const auto exactActions =
        CalculateArchiveActionRowLayout(328.0F, 180.0F, 0.0F, 140.0F, 8.0F, 0.0F);
    const auto narrowActions =
        CalculateArchiveActionRowLayout(327.0F, 180.0F, 0.0F, 140.0F, 8.0F, 0.0F);
    const auto minimalActions =
        CalculateArchiveActionRowLayout(0.0F, 180.0F, 0.0F, 140.0F, 8.0F, 0.0F);
    expect(exactActions.sameLine && !narrowActions.sameLine &&
               minimalActions.firstWidth == 1.0F &&
               minimalActions.secondWidth == 1.0F && !minimalActions.sameLine,
           "open and extract actions pair exactly, stack when narrow, and remain reachable");
}

void testArchiveMetadataLayout()
{
    using rerevved::tools::CalculateArchiveLabeledControlWidth;

    expect(CalculateArchiveLabeledControlWidth(500.0F, 112.0F, 0.0F, 0.0F) ==
               112.0F,
           "wide archive metadata action retains its natural width");
    expect(CalculateArchiveLabeledControlWidth(112.0F, 112.0F, 0.0F, 0.0F) ==
               112.0F,
           "exact-fit archive metadata action retains its natural width");
    expect(CalculateArchiveLabeledControlWidth(111.0F, 112.0F, 0.0F, 0.0F) ==
               111.0F,
           "narrow archive metadata action is bounded to available width");
    expect(CalculateArchiveLabeledControlWidth(0.0F, 112.0F, 0.0F, 0.0F) == 1.0F &&
               CalculateArchiveLabeledControlWidth(-20.0F, 112.0F, 0.0F, 0.0F) == 1.0F,
           "non-positive archive metadata widths retain a positive action");
}

void testFpkEntryRangeFormatting()
{
    using rerevved::tools::FormatBytes;
    using rerevved::tools::FormatFpkEntryRange;
    using rerevved::tools::FpkEntry;

    const FpkEntry entry{
        .offset = 62,
        .size   = 5,
    };
    expect(FormatFpkEntryRange(3, entry) ==
               "Entry: 3\nOffset: 62 (0x0000003E)\nSize: 5 bytes (0x00000005)\nEnd "
               "(exclusive): 67 (0x00000043)",
           "FPK entry range formats decimal and hexadecimal values");

    const FpkEntry maximumRange{
        .offset = 0xFFFFFFF0U,
        .size   = 0xFU,
    };
    expect(FormatFpkEntryRange(1, maximumRange) ==
               "Entry: 1\nOffset: 4294967280 (0xFFFFFFF0)\nSize: 15 bytes (0x0000000F)\n"
               "End (exclusive): 4294967295 (0xFFFFFFFF)",
           "FPK entry range computes the exclusive end without overflow");

    const FpkEntry maximumExclusiveEnd{
        .offset = std::numeric_limits<std::uint32_t>::max(),
        .size   = std::numeric_limits<std::uint32_t>::max(),
    };
    expect(FormatFpkEntryRange(1, maximumExclusiveEnd) ==
               "Entry: 1\nOffset: 4294967295 (0xFFFFFFFF)\nSize: 4294967295 bytes "
               "(0xFFFFFFFF)\nEnd (exclusive): 8589934590 (0x1FFFFFFFE)",
           "FPK entry range preserves the maximum promoted exclusive end");

    const FpkEntry emptyRange{};
    expect(FormatFpkEntryRange(1, emptyRange) ==
               "Entry: 1\nOffset: 0 (0x00000000)\nSize: 0 bytes (0x00000000)\nEnd "
               "(exclusive): 0 (0x00000000)",
           "empty FPK entry range preserves its zero-width exclusive boundary");

    const std::array<std::byte, 5> mixedBytes{
        std::byte{ 0x00 },
        std::byte{ 0x0A },
        std::byte{ 0x7F },
        std::byte{ 0xA5 },
        std::byte{ 0xFF },
    };
    expect(FormatBytes(mixedBytes) == "00 0A 7F A5 FF",
           "archive bytes preserve order, uppercase tokens, and single spaces");
    expect(FormatBytes(std::span<const std::byte>{}) == "",
           "empty archive bytes format as an empty string");
}

void testInspection()
{
    rerevved::tools::FileInspection emptyInspection{};
    expect(rerevved::tools::FormatHeader(emptyInspection).empty(),
           "empty inspection headers format as an empty string");

    rerevved::tools::FileInspection fullInspection{};
    for (std::size_t index = 0; index < fullInspection.header.size(); ++index)
    {
        fullInspection.header[index] = static_cast<std::byte>(index);
    }
    fullInspection.headerSize = fullInspection.header.size();
    expect(rerevved::tools::FormatHeader(fullInspection) ==
               "00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F",
           "full inspection headers format all retained bytes");

    const auto                         path = std::filesystem::temp_directory_path() /
                                              "rerevved-tools-core-test.dds";
    const std::array<unsigned char, 5> contents{ 0x44, 0x44, 0x53, 0x20, 0x7F };
    {
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(contents.data()),
                     static_cast<std::streamsize>(contents.size()));
    }

    const auto inspection = rerevved::tools::InspectFile(path);
    expect(inspection.has_value(), "regular file inspection succeeds");
    if (inspection)
    {
        expect(inspection->size == contents.size(), "file size is preserved");
        expect(inspection->headerSize == contents.size(), "short header size is preserved");
        expect(rerevved::tools::FormatHeader(*inspection) == "44 44 53 20 7F",
               "header formatting");
        expect(rerevved::tools::FormatByteSize(2048) == "2.00 KiB",
               "byte size formatting");
    }

    std::error_code ignored;
    const bool      removed = std::filesystem::remove(path, ignored);
    expect(removed && !ignored, "synthetic inspection file is removed before failure tests");

    const auto missing = rerevved::tools::InspectFile(path);
    expect(!missing && !missing.error().empty(),
           "missing file inspection reports a recoverable path error");

    const auto directory = rerevved::tools::InspectFile(std::filesystem::temp_directory_path());
    expect(!directory && directory.error() == "Path is not a regular file.",
           "directory inspection reports the stable non-file error");
}

void appendU32Le(std::vector<std::byte>& bytes, std::uint32_t value)
{
    for (unsigned int shift = 0; shift < 32; shift += 8)
    {
        bytes.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
    }
}

void appendU16Le(std::vector<std::byte>& bytes, std::uint16_t value)
{
    bytes.push_back(static_cast<std::byte>(value & 0xFFU));
    bytes.push_back(static_cast<std::byte>(value >> 8U));
}

void appendU16Be(std::vector<std::byte>& bytes, std::uint16_t value)
{
    bytes.push_back(static_cast<std::byte>(value >> 8U));
    bytes.push_back(static_cast<std::byte>(value & 0xFFU));
}

void appendU32Be(std::vector<std::byte>& bytes, std::uint32_t value)
{
    for (unsigned int shift = 32; shift > 0; shift -= 8)
    {
        bytes.push_back(static_cast<std::byte>((value >> (shift - 8U)) & 0xFFU));
    }
}

void writeU16Be(std::vector<std::byte>& bytes, std::size_t offset, std::uint16_t value)
{
    bytes[offset]     = static_cast<std::byte>(value >> 8U);
    bytes[offset + 1] = static_cast<std::byte>(value & 0xFFU);
}

void writeU32Be(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value)
{
    for (unsigned int shift = 32; shift > 0; shift -= 8)
    {
        bytes[offset++] = static_cast<std::byte>((value >> (shift - 8U)) & 0xFFU);
    }
}

void writeU32Le(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value)
{
    for (unsigned int shift = 0; shift < 32; shift += 8)
    {
        bytes[offset++] = static_cast<std::byte>((value >> shift) & 0xFFU);
    }
}

std::vector<std::byte> byteString(std::string_view value)
{
    std::vector<std::byte> bytes;
    bytes.reserve(value.size());
    for (const auto character : value)
    {
        bytes.push_back(static_cast<std::byte>(character));
    }
    return bytes;
}

void appendLengthPrefixed(std::vector<std::byte>& bytes, std::span<const std::byte> value)
{
    bytes.push_back(static_cast<std::byte>(value.size()));
    bytes.insert(bytes.end(), value.begin(), value.end());
}

void appendBits(std::vector<bool>& bits, std::uint32_t value, unsigned int bitCount)
{
    for (unsigned int remaining = bitCount; remaining > 0; --remaining)
    {
        bits.push_back(((value >> (remaining - 1U)) & 1U) != 0);
    }
}

void appendRect(std::vector<std::byte>&            bytes,
                const std::array<std::int32_t, 4>& coordinates,
                unsigned int                       bitCount)
{
    std::vector<bool> bits;
    appendBits(bits, bitCount, 5);
    const auto mask = (1U << bitCount) - 1U;
    for (const auto coordinate : coordinates)
    {
        appendBits(bits, static_cast<std::uint32_t>(coordinate) & mask, bitCount);
    }
    while (bits.size() % 8 != 0)
    {
        bits.push_back(false);
    }

    for (std::size_t offset = 0; offset < bits.size(); offset += 8)
    {
        std::uint8_t value = 0;
        for (std::size_t bit = 0; bit < 8; ++bit)
        {
            value = static_cast<std::uint8_t>((value << 1U) | (bits[offset + bit] ? 1U : 0U));
        }
        bytes.push_back(static_cast<std::byte>(value));
    }
}

void appendGfxTag(std::vector<std::byte>&    bytes,
                  std::uint16_t              code,
                  std::span<const std::byte> payload,
                  bool                       forceLong = false)
{
    const bool longForm = forceLong || payload.size() >= 0x3F;
    appendU16Le(bytes,
                static_cast<std::uint16_t>((code << 6U) | (longForm ? 0x3FU : payload.size())));
    if (longForm)
    {
        appendU32Le(bytes, static_cast<std::uint32_t>(payload.size()));
    }
    bytes.insert(bytes.end(), payload.begin(), payload.end());
}

std::vector<std::byte> makeExporterInfo(std::uint16_t version = 0x0204)
{
    std::vector<std::byte> payload;
    appendU16Le(payload, version);
    appendU32Le(payload, 5);
    appendU16Le(payload, 2);
    appendLengthPrefixed(payload, byteString("ui/"));
    appendLengthPrefixed(payload, byteString("menu"));
    return payload;
}

std::vector<std::byte>
makeExternalImage(std::span<const std::byte> exportName, std::span<const std::byte> fileName)
{
    std::vector<std::byte> payload;
    appendU16Le(payload, 7);
    appendU16Le(payload, 2);
    appendU16Le(payload, 64);
    appendU16Le(payload, 32);
    appendLengthPrefixed(payload, exportName);
    appendLengthPrefixed(payload, fileName);
    return payload;
}

std::vector<std::byte> beginGfx()
{
    std::vector<std::byte> bytes{
        std::byte{ 'G' },
        std::byte{ 'F' },
        std::byte{ 'X' },
        std::byte{ 8 },
        std::byte{ 0 },
        std::byte{ 0 },
        std::byte{ 0 },
        std::byte{ 0 },
    };
    appendRect(bytes, { -20, 1980, -40, 960 }, 12);
    appendU16Le(bytes, 0x1800);
    appendU16Le(bytes, 2);
    return bytes;
}

void updateGfxLength(std::vector<std::byte>& bytes)
{
    writeU32Le(bytes, 4, static_cast<std::uint32_t>(bytes.size()));
}

void finishGfx(std::vector<std::byte>& bytes)
{
    appendGfxTag(bytes, 0, {});
    updateGfxLength(bytes);
}

std::vector<std::byte> makeGfx()
{
    auto bytes    = beginGfx();
    auto exporter = makeExporterInfo();
    appendGfxTag(bytes, 1000, exporter);
    const std::array exportName{
        std::byte{ 'i' },
        std::byte{ 0 },
        std::byte{ 0xFF },
    };
    auto image = makeExternalImage(exportName, byteString("icon.dds"));
    appendGfxTag(bytes, 1001, image, true);
    appendGfxTag(bytes, 77, std::array{ std::byte{ 0xA5 } });
    appendGfxTag(bytes, 1001, image);
    finishGfx(bytes);
    return bytes;
}

std::vector<std::byte> makeNavigationGfx()
{
    auto bytes    = beginGfx();
    auto exporter = makeExporterInfo();
    appendGfxTag(bytes, 1000, exporter);

    std::vector<std::byte> exportName{
        std::byte{ 'm' }, std::byte{ '#' }, std::byte{ '#' }, std::byte{ '\\' }, std::byte{ 0 }
    };
    std::vector<std::byte> fileName;
    fileName.reserve(120);
    for (std::size_t index = 0; index < 100; ++index)
    {
        fileName.push_back(std::byte{ 'L' });
    }
    fileName.insert(fileName.end(),
                    { std::byte{ '#' }, std::byte{ '#' }, std::byte{ '\\' }, std::byte{ 0 }, std::byte{ 0x1F } });
    const auto image = makeExternalImage(exportName, fileName);
    appendGfxTag(bytes, 1001, image, true);
    appendGfxTag(bytes, 1001, image);
    finishGfx(bytes);
    return bytes;
}

void appendNifSizedBytes(std::vector<std::byte>& bytes, std::span<const std::byte> value)
{
    appendU32Be(bytes, static_cast<std::uint32_t>(value.size()));
    bytes.insert(bytes.end(), value.begin(), value.end());
}

struct SyntheticNif
{
    std::vector<std::byte> bytes;
    std::size_t            blockTypeCountOffset     = 0;
    std::size_t            firstTypeIndexOffset     = 0;
    std::size_t            firstBlockSizeOffset     = 0;
    std::size_t            stringCountOffset        = 0;
    std::size_t            maxStringLengthOffset    = 0;
    std::size_t            groupCountOffset         = 0;
    std::size_t            rootCountOffset          = 0;
    std::size_t            rootIndexOffset          = 0;
    std::size_t            nodeNameIndexOffset      = 0;
    std::size_t            nodeExtraCountOffset     = 0;
    std::size_t            nodeChildOffset          = 0;
    std::size_t            shapeDataOffset          = 0;
    std::size_t            shapeMaterialCountOffset = 0;
};

void appendNifAvObject(std::vector<std::byte>& bytes,
                       std::uint32_t           nameIndex,
                       std::uint16_t           flags,
                       std::uint32_t           property)
{
    appendU32Be(bytes, nameIndex);
    appendU32Be(bytes, 1);
    appendU32Be(bytes, 2);
    appendU32Be(bytes, 0xFFFFFFFF);
    appendU16Be(bytes, flags);
    appendU32Be(bytes, 0x3F800000);
    appendU32Be(bytes, 0x40000000);
    appendU32Be(bytes, 0x40400000);
    for (std::size_t index = 0; index < 9; ++index)
    {
        appendU32Be(bytes, index % 4 == 0 ? 0x3F800000 : 0);
    }
    appendU32Be(bytes, 0x40000000);
    appendU32Be(bytes, 1);
    appendU32Be(bytes, property);
    appendU32Be(bytes, 0xFFFFFFFF);
}

SyntheticNif makeNif()
{
    SyntheticNif result{};
    auto&        bytes  = result.bytes;
    const auto   header = byteString("Gamebryo File Format, Version 20.3.0.9\n");
    bytes.insert(bytes.end(), header.begin(), header.end());
    appendU32Le(bytes, 0x14030009);
    bytes.push_back(std::byte{ 0 });
    appendU32Le(bytes, 0);
    appendU32Le(bytes, 3);

    result.blockTypeCountOffset = bytes.size();
    appendU16Be(bytes, 3);
    appendNifSizedBytes(bytes, byteString("NiNode"));
    appendNifSizedBytes(bytes, byteString("NiTriShape"));
    appendNifSizedBytes(bytes, byteString("OpaqueBlock"));
    result.firstTypeIndexOffset = bytes.size();
    appendU16Be(bytes, 0);
    appendU16Be(bytes, 1);
    appendU16Be(bytes, 2);

    std::vector<std::byte> node;
    result.nodeNameIndexOffset  = 0;
    result.nodeExtraCountOffset = 4;
    appendNifAvObject(node, 0, 0x1234, 2);
    appendU32Be(node, 1);
    result.nodeChildOffset = node.size();
    appendU32Be(node, 1);
    appendU32Be(node, 0);

    std::vector<std::byte> shape;
    appendNifAvObject(shape, 1, 0x5678, 2);
    result.shapeDataOffset = shape.size();
    appendU32Be(shape, 2);
    appendU32Be(shape, 0xFFFFFFFF);
    result.shapeMaterialCountOffset = shape.size();
    appendU32Be(shape, 1);
    appendU32Be(shape, 0);
    appendU32Be(shape, 0xFFFFFFF9);
    appendU32Be(shape, 0);
    shape.push_back(std::byte{ 1 });

    const std::array opaque{ std::byte{ 0xB1 }, std::byte{ 0xB2 }, std::byte{ 0xB3 } };
    result.firstBlockSizeOffset = bytes.size();
    appendU32Be(bytes, static_cast<std::uint32_t>(node.size()));
    appendU32Be(bytes, static_cast<std::uint32_t>(shape.size()));
    appendU32Be(bytes, static_cast<std::uint32_t>(opaque.size()));

    result.stringCountOffset = bytes.size();
    appendU32Be(bytes, 2);
    result.maxStringLengthOffset = bytes.size();
    appendU32Be(bytes, 4);
    appendNifSizedBytes(bytes, byteString("Root"));
    const std::array rawString{ std::byte{ 0 }, std::byte{ 0xFF } };
    appendNifSizedBytes(bytes, rawString);
    result.groupCountOffset = bytes.size();
    appendU32Be(bytes, 1);
    appendU32Be(bytes, 3);

    result.nodeNameIndexOffset += bytes.size();
    result.nodeExtraCountOffset += bytes.size();
    result.nodeChildOffset += bytes.size();
    bytes.insert(bytes.end(), node.begin(), node.end());
    result.shapeDataOffset += bytes.size();
    result.shapeMaterialCountOffset += bytes.size();
    bytes.insert(bytes.end(), shape.begin(), shape.end());
    bytes.insert(bytes.end(), opaque.begin(), opaque.end());
    result.rootCountOffset = bytes.size();
    appendU32Be(bytes, 1);
    result.rootIndexOffset = bytes.size();
    appendU32Be(bytes, 0);
    return result;
}

struct SyntheticGeometryNif
{
    std::vector<std::byte> bytes;
    std::size_t            blockSizeOffset             = 0;
    std::size_t            blockPayloadOffset          = 0;
    std::size_t            vertexCountOffset           = 0;
    std::size_t            vertexPositionsOffset       = 0;
    std::size_t            dataFlagsOffset             = 0;
    std::size_t            hasNormalsOffset            = 0;
    std::size_t            normalVectorsOffset         = 0;
    std::size_t            additionalDataOffset        = 0;
    std::size_t            triangleCountOffset         = 0;
    std::size_t            triangleIndicesOffset       = 0;
    std::size_t            matchGroupCountOffset       = 0;
    std::size_t            matchGroupVertexCountOffset = 0;
    std::size_t            matchGroupSelectorsOffset   = 0;
};

SyntheticGeometryNif makeGeometryNif(bool populated)
{
    SyntheticGeometryNif   result{};
    std::vector<std::byte> payload;
    appendU32Be(payload, 0xFFFFFFF9);
    result.vertexCountOffset = payload.size();
    appendU16Be(payload, populated ? 2 : 0);
    payload.push_back(std::byte{ 0xA1 });
    payload.push_back(std::byte{ 0xB2 });
    payload.push_back(populated ? std::byte{ 0xFF } : std::byte{ 0 });
    result.vertexPositionsOffset = payload.size();
    if (populated)
    {
        payload.insert(payload.end(), 24, std::byte{ 0 });
    }
    result.dataFlagsOffset = payload.size();
    appendU16Be(payload, populated ? 0x1001 : 0);
    result.hasNormalsOffset = payload.size();
    payload.push_back(populated ? std::byte{ 2 } : std::byte{ 0 });
    result.normalVectorsOffset = payload.size();
    if (populated)
    {
        payload.insert(payload.end(), 24, std::byte{ 0 });
        payload.insert(payload.end(), 24, std::byte{ 0 });
        payload.insert(payload.end(), 24, std::byte{ 0 });
    }
    appendU32Be(payload, 0x3F800000);
    appendU32Be(payload, 0x40000000);
    appendU32Be(payload, 0x40400000);
    appendU32Be(payload, 0xC0800000);
    payload.push_back(populated ? std::byte{ 0x80 } : std::byte{ 0 });
    if (populated)
    {
        payload.insert(payload.end(), 32, std::byte{ 0 });
    }
    if (populated)
    {
        payload.insert(payload.end(), 16, std::byte{ 0 });
    }
    appendU16Be(payload, 0xBEEF);
    result.additionalDataOffset = payload.size();
    appendU32Be(payload, 0xFFFFFFFF);
    result.triangleCountOffset = payload.size();
    appendU16Be(payload, populated ? 1 : 0);
    appendU32Be(payload, 7);
    payload.push_back(populated ? std::byte{ 0x7F } : std::byte{ 0 });
    result.triangleIndicesOffset = payload.size();
    if (populated)
    {
        appendU16Be(payload, 0);
        appendU16Be(payload, 1);
        appendU16Be(payload, 0);
    }
    result.matchGroupCountOffset = payload.size();
    appendU16Be(payload, populated ? 2 : 0);
    result.matchGroupVertexCountOffset = payload.size();
    if (populated)
    {
        appendU16Be(payload, 2);
        result.matchGroupSelectorsOffset = payload.size();
        appendU16Be(payload, 0);
        appendU16Be(payload, 7);
        appendU16Be(payload, 3);
        appendU16Be(payload, 5);
        appendU16Be(payload, 1);
        appendU16Be(payload, 9);
    }

    auto&      bytes  = result.bytes;
    const auto header = byteString("Gamebryo File Format, Version 20.3.0.9\n");
    bytes.insert(bytes.end(), header.begin(), header.end());
    appendU32Le(bytes, 0x14030009);
    bytes.push_back(std::byte{ 0 });
    appendU32Le(bytes, 0);
    appendU32Le(bytes, 1);
    appendU16Be(bytes, 1);
    appendNifSizedBytes(bytes, byteString("NiTriShapeData"));
    appendU16Be(bytes, 0);
    result.blockSizeOffset = bytes.size();
    appendU32Be(bytes, static_cast<std::uint32_t>(payload.size()));
    appendU32Be(bytes, 0);
    appendU32Be(bytes, 0);
    appendU32Be(bytes, 0);

    result.vertexCountOffset += bytes.size();
    result.vertexPositionsOffset += bytes.size();
    result.dataFlagsOffset += bytes.size();
    result.hasNormalsOffset += bytes.size();
    result.normalVectorsOffset += bytes.size();
    result.additionalDataOffset += bytes.size();
    result.triangleCountOffset += bytes.size();
    result.triangleIndicesOffset += bytes.size();
    result.matchGroupCountOffset += bytes.size();
    result.matchGroupVertexCountOffset += bytes.size();
    result.matchGroupSelectorsOffset += bytes.size();
    result.blockPayloadOffset = bytes.size();
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    appendU32Be(bytes, 1);
    appendU32Be(bytes, 0);
    return result;
}

void appendNifF32(std::vector<std::byte>& bytes, float value)
{
    appendU32Be(bytes, std::bit_cast<std::uint32_t>(value));
}

void writeNifF32(std::vector<std::byte>& bytes, std::size_t offset, float value)
{
    writeU32Be(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

void appendNifVector3(std::vector<std::byte>& bytes, float x, float y, float z)
{
    appendNifF32(bytes, x);
    appendNifF32(bytes, y);
    appendNifF32(bytes, z);
}

void appendSyntheticObjectNet(std::vector<std::byte>& bytes, std::uint32_t nameIndex)
{
    appendU32Be(bytes, nameIndex);
    appendU32Be(bytes, 0);
    appendU32Be(bytes, 0xFFFFFFFF);
}

std::size_t appendSyntheticAvObject(std::vector<std::byte>&        bytes,
                                    std::uint32_t                  nameIndex,
                                    std::span<const std::uint32_t> properties)
{
    appendSyntheticObjectNet(bytes, nameIndex);
    appendU16Be(bytes, 0);
    appendNifVector3(bytes, 0.0F, 0.0F, 0.0F);
    for (std::size_t index = 0; index < 9; ++index)
    {
        appendNifF32(bytes, index % 4 == 0 ? 1.0F : 0.0F);
    }
    appendNifF32(bytes, 1.0F);
    appendU32Be(bytes, static_cast<std::uint32_t>(properties.size()));
    const auto propertyReferenceOffset = bytes.size();
    for (const auto property : properties)
    {
        appendU32Be(bytes, property);
    }
    appendU32Be(bytes, 0xFFFFFFFF);
    return propertyReferenceOffset;
}

struct SyntheticModelNif
{
    std::vector<std::byte> bytes;
    std::size_t            rootReferenceOffset     = 0;
    std::size_t            childReferenceOffset    = 0;
    std::size_t            dataReferenceOffset     = 0;
    std::size_t            propertyReferenceOffset = 0;
    std::size_t            triangleIndicesOffset   = 0;
    std::size_t            nodeTransformOffset     = 0;
    std::size_t            shapeTransformOffset    = 0;
};

SyntheticModelNif makeSyntheticModelNif()
{
    SyntheticModelNif result{};

    std::vector<std::byte> node;
    result.nodeTransformOffset = 14;
    appendSyntheticAvObject(node, 0, {});
    appendU32Be(node, 1);
    result.childReferenceOffset = node.size();
    appendU32Be(node, 1);
    appendU32Be(node, 0);

    std::vector<std::byte> shape;
    result.shapeTransformOffset = 14;
    const std::array shapeProperties{ std::uint32_t{ 3 } };
    result.propertyReferenceOffset =
        appendSyntheticAvObject(shape, 1, shapeProperties);
    result.dataReferenceOffset = shape.size();
    appendU32Be(shape, 2);
    appendU32Be(shape, 0xFFFFFFFF);
    appendU32Be(shape, 0);
    appendU32Be(shape, 0xFFFFFFFF);
    shape.push_back(std::byte{ 0 });

    std::vector<std::byte> geometry;
    appendU32Be(geometry, 0);
    appendU16Be(geometry, 3);
    geometry.push_back(std::byte{ 0 });
    geometry.push_back(std::byte{ 0 });
    geometry.push_back(std::byte{ 1 });
    appendNifVector3(geometry, 0.0F, 0.0F, 0.0F);
    appendNifVector3(geometry, 1.0F, 0.0F, 0.0F);
    appendNifVector3(geometry, 0.0F, 1.0F, 0.0F);
    appendU16Be(geometry, 0);
    geometry.push_back(std::byte{ 1 });
    appendNifVector3(geometry, 1.0F, 0.0F, 0.0F);
    appendNifVector3(geometry, 0.0F, 1.0F, 0.0F);
    appendNifVector3(geometry, 0.0F, 0.0F, 1.0F);
    appendNifVector3(geometry, 0.5F, 0.5F, 0.0F);
    appendNifF32(geometry, 1.0F);
    geometry.push_back(std::byte{ 0 });
    appendU16Be(geometry, 0);
    appendU32Be(geometry, 0xFFFFFFFF);
    appendU16Be(geometry, 1);
    appendU32Be(geometry, 3);
    geometry.push_back(std::byte{ 1 });
    result.triangleIndicesOffset = geometry.size();
    appendU16Be(geometry, 0);
    appendU16Be(geometry, 1);
    appendU16Be(geometry, 2);
    appendU16Be(geometry, 0);

    std::vector<std::byte> material;
    appendSyntheticObjectNet(material, 2);
    appendNifVector3(material, 1.0F, 1.0F, 1.0F);
    appendNifVector3(material, 1.0F, 1.0F, 1.0F);
    appendNifVector3(material, 0.0F, 0.0F, 0.0F);
    appendNifVector3(material, 0.0F, 0.0F, 0.0F);
    appendNifF32(material, 1.0F);
    appendNifF32(material, 1.0F);

    auto&      bytes  = result.bytes;
    const auto header = byteString("Gamebryo File Format, Version 20.3.0.9\n");
    bytes.insert(bytes.end(), header.begin(), header.end());
    appendU32Le(bytes, 0x14030009);
    bytes.push_back(std::byte{ 0 });
    appendU32Le(bytes, 0);
    appendU32Le(bytes, 4);
    appendU16Be(bytes, 4);
    appendNifSizedBytes(bytes, byteString("NiNode"));
    appendNifSizedBytes(bytes, byteString("NiTriShape"));
    appendNifSizedBytes(bytes, byteString("NiTriShapeData"));
    appendNifSizedBytes(bytes, byteString("NiMaterialProperty"));
    appendU16Be(bytes, 0);
    appendU16Be(bytes, 1);
    appendU16Be(bytes, 2);
    appendU16Be(bytes, 3);
    appendU32Be(bytes, static_cast<std::uint32_t>(node.size()));
    appendU32Be(bytes, static_cast<std::uint32_t>(shape.size()));
    appendU32Be(bytes, static_cast<std::uint32_t>(geometry.size()));
    appendU32Be(bytes, static_cast<std::uint32_t>(material.size()));
    appendU32Be(bytes, 3);
    appendU32Be(bytes, 17);
    appendNifSizedBytes(bytes, byteString("SyntheticRoot"));
    appendNifSizedBytes(bytes, byteString("SyntheticTriangle"));
    appendNifSizedBytes(bytes, byteString("SyntheticMaterial"));
    appendU32Be(bytes, 0);

    result.childReferenceOffset += bytes.size();
    result.nodeTransformOffset += bytes.size();
    bytes.insert(bytes.end(), node.begin(), node.end());
    result.propertyReferenceOffset += bytes.size();
    result.dataReferenceOffset += bytes.size();
    result.shapeTransformOffset += bytes.size();
    bytes.insert(bytes.end(), shape.begin(), shape.end());
    result.triangleIndicesOffset += bytes.size();
    bytes.insert(bytes.end(), geometry.begin(), geometry.end());
    bytes.insert(bytes.end(), material.begin(), material.end());
    appendU32Be(bytes, 1);
    result.rootReferenceOffset = bytes.size();
    appendU32Be(bytes, 0);
    return result;
}

constexpr std::array<std::uint32_t, 14> kSyntheticMaterialBitsA{
    0x3FA00000,
    0xC0200000,
    0x40700000,
    0xC0900000,
    0x40A80000,
    0xC0D80000,
    0x00000000,
    0x80000000,
    0x7FC12345,
    0x7F800000,
    0xFF800000,
    0x00000001,
    0x40F00000,
    0xC1040000,
};

constexpr std::array<std::uint32_t, 14> kSyntheticMaterialBitsB{
    0x41200000,
    0x41300000,
    0x41400000,
    0x41500000,
    0x41600000,
    0x41700000,
    0x41800000,
    0x41880000,
    0x41900000,
    0x41980000,
    0x41A00000,
    0x41A80000,
    0x41B00000,
    0x41B80000,
};

void appendSyntheticMaterialProperty(std::vector<std::byte>&              bytes,
                                     std::uint32_t                        nameIndex,
                                     const std::array<std::uint32_t, 14>& bits)
{
    appendSyntheticObjectNet(bytes, nameIndex);
    for (const auto value : bits)
    {
        appendU32Be(bytes, value);
    }
}

struct SyntheticMaterialNif
{
    std::vector<std::byte>     bytes;
    std::array<std::size_t, 2> blockSizeOffsets{};
    std::array<std::size_t, 2> payloadOffsets{};
};

SyntheticMaterialNif makeSyntheticMaterialNif()
{
    SyntheticMaterialNif                  result{};
    std::array<std::vector<std::byte>, 2> materials;
    appendSyntheticMaterialProperty(materials[0], 0, kSyntheticMaterialBitsA);
    appendSyntheticMaterialProperty(materials[1], 1, kSyntheticMaterialBitsB);

    auto&      bytes  = result.bytes;
    const auto header = byteString("Gamebryo File Format, Version 20.3.0.9\n");
    bytes.insert(bytes.end(), header.begin(), header.end());
    appendU32Le(bytes, 0x14030009);
    bytes.push_back(std::byte{ 0 });
    appendU32Le(bytes, 0);
    appendU32Le(bytes, 2);
    appendU16Be(bytes, 1);
    appendNifSizedBytes(bytes, byteString("NiMaterialProperty"));
    appendU16Be(bytes, 0);
    appendU16Be(bytes, 0);
    for (std::size_t index = 0; index < materials.size(); ++index)
    {
        result.blockSizeOffsets[index] = bytes.size();
        appendU32Be(bytes, static_cast<std::uint32_t>(materials[index].size()));
    }
    appendU32Be(bytes, 2);
    appendU32Be(bytes, 18);
    appendNifSizedBytes(bytes, byteString("SyntheticMaterialA"));
    appendNifSizedBytes(bytes, byteString("SyntheticMaterialB"));
    appendU32Be(bytes, 0);
    for (std::size_t index = 0; index < materials.size(); ++index)
    {
        result.payloadOffsets[index] = bytes.size();
        bytes.insert(bytes.end(), materials[index].begin(), materials[index].end());
    }
    appendU32Be(bytes, 0);
    return result;
}

std::size_t
appendSyntheticTexDesc(std::vector<std::byte>&           bytes,
                       std::uint32_t                     source,
                       std::uint16_t                     flags,
                       std::uint8_t                      hasTextureTransform,
                       std::span<const std::uint32_t, 8> transformBits)
{
    const auto sourceOffset = bytes.size();
    appendU32Be(bytes, source);
    appendU16Be(bytes, flags);
    bytes.push_back(static_cast<std::byte>(hasTextureTransform));
    if (hasTextureTransform == 1)
    {
        for (const auto bits : transformBits)
        {
            appendU32Be(bytes, bits);
        }
    }
    return sourceOffset;
}

struct SyntheticTextureNif
{
    std::vector<std::byte>     bytes;
    std::array<std::size_t, 5> blockSizeOffsets{};
    std::array<std::size_t, 5> payloadOffsets{};
    std::array<std::size_t, 5> payloadSizes{};
    std::size_t                firstPropertyNameOffset       = 0;
    std::size_t                firstPropertyControllerOffset = 0;
    std::size_t                textureCountOffset            = 0;
    std::array<std::size_t, 6> descriptorSourceOffsets{};
    std::size_t                shaderCountOffset = 0;
    std::array<std::size_t, 2> sourceNameOffsets{};
    std::array<std::size_t, 2> sourcePixelOffsets{};
    std::array<std::size_t, 2> sourceDerivedOffsets{};
};

SyntheticTextureNif makeSyntheticTextureNif()
{
    constexpr std::array<std::uint32_t, 8> kTransformBitsA{
        0x00000000,
        0x80000000,
        0x7F800000,
        0xFF800000,
        0x7FC12345,
        0xA5A5A5A5,
        0x3F000000,
        0xBF000000,
    };
    constexpr std::array<std::uint32_t, 8> kTransformBitsB{
        0x3F800000,
        0x40000000,
        0x40400000,
        0x40800000,
        0x40A00000,
        0x12345678,
        0x40C00000,
        0x40E00000,
    };
    constexpr std::array<std::uint32_t, 6> kBumpBits{
        0x80000000,
        0x7F800000,
        0xFF800000,
        0x7FC54321,
        0x00000001,
        0xBF800000,
    };

    SyntheticTextureNif                   result{};
    std::array<std::vector<std::byte>, 5> payloads;

    auto& firstProperty            = payloads[0];
    result.firstPropertyNameOffset = firstProperty.size();
    appendSyntheticObjectNet(firstProperty, 0xFFFFFFFF);
    result.firstPropertyControllerOffset = 8;
    appendU16Be(firstProperty, 0xA5F3);
    result.textureCountOffset = firstProperty.size();
    appendU32Be(firstProperty, 9);

    firstProperty.push_back(std::byte{ 1 });
    result.descriptorSourceOffsets[0] =
        appendSyntheticTexDesc(firstProperty, 2, 0xABCD, 1, kTransformBitsA);
    firstProperty.push_back(std::byte{ 2 });
    firstProperty.push_back(std::byte{ 0 });
    firstProperty.push_back(std::byte{ 0x7F });
    firstProperty.push_back(std::byte{ 1 });
    result.descriptorSourceOffsets[1] =
        appendSyntheticTexDesc(firstProperty, 3, 0x1020, 2, kTransformBitsA);
    firstProperty.push_back(std::byte{ 1 });
    result.descriptorSourceOffsets[2] =
        appendSyntheticTexDesc(firstProperty, 2, 0x3040, 0, kTransformBitsA);
    for (const auto bits : kBumpBits)
    {
        appendU32Be(firstProperty, bits);
    }
    firstProperty.push_back(std::byte{ 0 });
    firstProperty.push_back(std::byte{ 1 });
    result.descriptorSourceOffsets[3] =
        appendSyntheticTexDesc(firstProperty, 0xFFFFFFFF, 0x5060, 0, kTransformBitsA);
    appendU32Be(firstProperty, 0x7FC0BEEF);
    firstProperty.push_back(std::byte{ 0 });

    result.shaderCountOffset = firstProperty.size();
    appendU32Be(firstProperty, 3);
    firstProperty.push_back(std::byte{ 1 });
    result.descriptorSourceOffsets[4] =
        appendSyntheticTexDesc(firstProperty, 2, 0x7080, 0, kTransformBitsA);
    appendU32Be(firstProperty, 0xDEADBEEF);
    firstProperty.push_back(std::byte{ 2 });
    firstProperty.push_back(std::byte{ 1 });
    result.descriptorSourceOffsets[5] =
        appendSyntheticTexDesc(firstProperty, 3, 0x90A0, 1, kTransformBitsB);
    appendU32Be(firstProperty, 0xCAFEBABE);

    auto& secondProperty = payloads[1];
    appendSyntheticObjectNet(secondProperty, 0xFFFFFFFF);
    appendU16Be(secondProperty, 0x55AA);
    appendU32Be(secondProperty, 9);
    for (const auto presence : { 0U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U })
    {
        secondProperty.push_back(static_cast<std::byte>(presence));
    }
    appendU32Be(secondProperty, 0);

    auto appendSource = [&](std::size_t   index,
                            std::uint8_t  useExternal,
                            std::uint32_t fileName,
                            std::uint32_t pixelData,
                            std::uint32_t baseValue)
    {
        auto& source = payloads[index + 2];
        appendSyntheticObjectNet(source, 0xFFFFFFFF);
        result.sourceDerivedOffsets[index] = source.size();
        source.push_back(static_cast<std::byte>(useExternal));
        result.sourceNameOffsets[index] = source.size();
        appendU32Be(source, fileName);
        result.sourcePixelOffsets[index] = source.size();
        appendU32Be(source, pixelData);
        appendU32Be(source, baseValue + 0);
        appendU32Be(source, baseValue + 1);
        appendU32Be(source, baseValue + 2);
        source.push_back(std::byte{ 2 });
        source.push_back(std::byte{ 3 });
        source.push_back(std::byte{ 4 });
    };
    appendSource(0, 1, 0, 0xFFFFFFFF, 0x11111110);
    appendSource(1, 0, 0xFFFFFFFF, 4, 0x22222220);
    payloads[4].push_back(std::byte{ 0xA5 });

    auto&      bytes  = result.bytes;
    const auto header = byteString("Gamebryo File Format, Version 20.3.0.9\n");
    bytes.insert(bytes.end(), header.begin(), header.end());
    appendU32Le(bytes, 0x14030009);
    bytes.push_back(std::byte{ 0 });
    appendU32Le(bytes, 0);
    appendU32Le(bytes, static_cast<std::uint32_t>(payloads.size()));
    appendU16Be(bytes, 3);
    appendNifSizedBytes(bytes, byteString("NiTexturingProperty"));
    appendNifSizedBytes(bytes, byteString("NiSourceTexture"));
    appendNifSizedBytes(bytes, byteString("OpaqueBlock"));
    for (const auto type : { 0U, 0U, 1U, 1U, 2U })
    {
        appendU16Be(bytes, static_cast<std::uint16_t>(type));
    }
    for (std::size_t index = 0; index < payloads.size(); ++index)
    {
        result.blockSizeOffsets[index] = bytes.size();
        result.payloadSizes[index]     = payloads[index].size();
        appendU32Be(bytes, static_cast<std::uint32_t>(payloads[index].size()));
    }
    appendU32Be(bytes, 1);
    appendU32Be(bytes, 20);
    appendNifSizedBytes(bytes, byteString("SyntheticTexture.dds"));
    appendU32Be(bytes, 0);

    for (std::size_t index = 0; index < payloads.size(); ++index)
    {
        result.payloadOffsets[index] = bytes.size();
        bytes.insert(bytes.end(), payloads[index].begin(), payloads[index].end());
    }
    appendU32Be(bytes, 0);

    const auto rebase = [&](std::size_t& value, std::size_t payloadIndex)
    {
        value += result.payloadOffsets[payloadIndex];
    };
    rebase(result.firstPropertyNameOffset, 0);
    rebase(result.firstPropertyControllerOffset, 0);
    rebase(result.textureCountOffset, 0);
    for (auto& value : result.descriptorSourceOffsets)
    {
        rebase(value, 0);
    }
    rebase(result.shaderCountOffset, 0);
    for (std::size_t index = 0; index < result.sourceNameOffsets.size(); ++index)
    {
        rebase(result.sourceNameOffsets[index], index + 2);
        rebase(result.sourcePixelOffsets[index], index + 2);
        rebase(result.sourceDerivedOffsets[index], index + 2);
    }
    return result;
}

std::vector<std::byte> makeSyntheticSourceNavigationNif()
{
    const std::vector<std::byte> duplicateName{
        std::byte{ 'D' }, std::byte{ 'u' }, std::byte{ 'p' }, std::byte{ '#' }, std::byte{ '#' }, std::byte{ '\\' }, std::byte{ 0 }, std::byte{ 0x1F }, std::byte{ '.' }, std::byte{ 'd' }, std::byte{ 'd' }, std::byte{ 's' }
    };
    std::vector<std::byte> longName;
    longName.reserve(128);
    for (std::size_t index = 0; index < 96; ++index)
    {
        longName.push_back(std::byte{ 'L' });
    }
    longName.insert(longName.end(),
                    { std::byte{ '#' }, std::byte{ '#' }, std::byte{ '\\' }, std::byte{ 0 }, std::byte{ 0xFF }, std::byte{ '.' }, std::byte{ 'd' }, std::byte{ 'd' }, std::byte{ 's' } });
    const std::array names{ duplicateName, duplicateName, longName };

    std::array<std::vector<std::byte>, 3> payloads;
    for (std::size_t index = 0; index < payloads.size(); ++index)
    {
        auto& source = payloads[index];
        appendSyntheticObjectNet(source, 0xFFFFFFFF);
        source.push_back(std::byte{ 1 });
        appendU32Be(source, static_cast<std::uint32_t>(index));
        appendU32Be(source, 0xFFFFFFFF);
        appendU32Be(source, 0x11110000U + static_cast<std::uint32_t>(index));
        appendU32Be(source, 0x22220000U + static_cast<std::uint32_t>(index));
        appendU32Be(source, 0x33330000U + static_cast<std::uint32_t>(index));
        source.push_back(std::byte{ 2 });
        source.push_back(std::byte{ 3 });
        source.push_back(std::byte{ 4 });
    }

    std::vector<std::byte> bytes;
    const auto             header = byteString("Gamebryo File Format, Version 20.3.0.9\n");
    bytes.insert(bytes.end(), header.begin(), header.end());
    appendU32Le(bytes, 0x14030009);
    bytes.push_back(std::byte{ 0 });
    appendU32Le(bytes, 0);
    appendU32Le(bytes, static_cast<std::uint32_t>(payloads.size()));
    appendU16Be(bytes, 1);
    appendNifSizedBytes(bytes, byteString("NiSourceTexture"));
    for (std::size_t index = 0; index < payloads.size(); ++index)
    {
        appendU16Be(bytes, 0);
    }
    for (const auto& payload : payloads)
    {
        appendU32Be(bytes, static_cast<std::uint32_t>(payload.size()));
    }
    appendU32Be(bytes, static_cast<std::uint32_t>(names.size()));
    appendU32Be(bytes, 128);
    for (const auto& name : names)
    {
        appendNifSizedBytes(bytes, name);
    }
    appendU32Be(bytes, 0);
    for (const auto& payload : payloads)
    {
        bytes.insert(bytes.end(), payload.begin(), payload.end());
    }
    appendU32Be(bytes, 0);
    return bytes;
}

std::vector<std::byte> makeDds(bool dx10)
{
    std::vector<std::byte> bytes(dx10 ? 148 : 128);
    bytes[0] = std::byte{ 'D' };
    bytes[1] = std::byte{ 'D' };
    bytes[2] = std::byte{ 'S' };
    bytes[3] = std::byte{ ' ' };
    writeU32Le(bytes, 4, 124);
    writeU32Le(bytes, 8, 0x0002100FU);
    writeU32Le(bytes, 12, 64);
    writeU32Le(bytes, 16, 128);
    writeU32Le(bytes, 20, 512);
    writeU32Le(bytes, 24, 4);
    writeU32Le(bytes, 28, 3);
    bytes[32] = std::byte{ 0xA1 };
    writeU32Le(bytes, 76, 32);
    writeU32Le(bytes, 80, dx10 ? 0x4U : 0x41U);
    if (dx10)
    {
        bytes[84] = std::byte{ 'D' };
        bytes[85] = std::byte{ 'X' };
        bytes[86] = std::byte{ '1' };
        bytes[87] = std::byte{ '0' };
    }
    writeU32Le(bytes, 88, 32);
    writeU32Le(bytes, 92, 0x00FF0000U);
    writeU32Le(bytes, 96, 0x0000FF00U);
    writeU32Le(bytes, 100, 0x000000FFU);
    writeU32Le(bytes, 104, 0xFF000000U);
    writeU32Le(bytes, 108, 0x00401008U);
    writeU32Le(bytes, 112, 0x00000200U);
    bytes[116] = std::byte{ 0xB2 };
    bytes[124] = std::byte{ 0xC3 };
    if (dx10)
    {
        writeU32Le(bytes, 128, 28);
        writeU32Le(bytes, 132, 3);
        writeU32Le(bytes, 136, 4);
        writeU32Le(bytes, 140, 2);
        writeU32Le(bytes, 144, 1);
    }
    return bytes;
}

std::vector<std::byte> makePreviewDds(std::uint32_t              width,
                                      std::uint32_t              height,
                                      std::uint32_t              redMask,
                                      std::uint32_t              greenMask,
                                      std::uint32_t              blueMask,
                                      std::uint32_t              alphaMask,
                                      std::span<const std::byte> pixels)
{
    auto bytes = makeDds(false);
    writeU32Le(bytes, 12, height);
    writeU32Le(bytes, 16, width);
    writeU32Le(bytes, 20, width * 4);
    writeU32Le(bytes, 80, alphaMask == 0 ? 0x40U : 0x41U);
    writeU32Le(bytes, 88, 32);
    writeU32Le(bytes, 92, redMask);
    writeU32Le(bytes, 96, greenMask);
    writeU32Le(bytes, 100, blueMask);
    writeU32Le(bytes, 104, alphaMask);
    writeU32Le(bytes, 112, 0);
    bytes.insert(bytes.end(), pixels.begin(), pixels.end());
    return bytes;
}

void testDdsMetadata()
{
    using rerevved::tools::DdsMetadataError;
    using rerevved::tools::ParseDdsMetadata;

    const auto legacy   = makeDds(false);
    const auto metadata = ParseDdsMetadata(legacy);
    expect(metadata.has_value(), "legacy DDS metadata parsing succeeds");
    if (metadata)
    {
        expect(metadata->width == 128 && metadata->height == 64,
               "DDS dimensions are preserved");
        expect(metadata->flags == 0x0002100FU && metadata->pitchOrLinearSize == 512 &&
                   metadata->depth == 4 && metadata->mipMapCount == 3,
               "DDS base header values are preserved");
        expect(metadata->reserved1[0] == std::byte{ 0xA1 } &&
                   metadata->capsReserved[0] == std::byte{ 0xB2 } &&
                   metadata->reserved2[0] == std::byte{ 0xC3 },
               "DDS reserved bytes are preserved");
        expect(metadata->pixelFormat.flags == 0x41U &&
                   metadata->pixelFormat.rgbBitCount == 32 &&
                   metadata->pixelFormat.redMask == 0x00FF0000U &&
                   metadata->pixelFormat.alphaMask == 0xFF000000U,
               "DDS pixel format metadata is preserved");
        expect(!metadata->dx10Header.has_value(),
               "legacy DDS metadata has no DX10 extension");
    }

    const auto dx10         = makeDds(true);
    const auto dx10Metadata = ParseDdsMetadata(dx10);
    expect(dx10Metadata.has_value() && dx10Metadata->dx10Header.has_value(),
           "DX10 DDS metadata parsing succeeds");
    if (dx10Metadata && dx10Metadata->dx10Header)
    {
        expect(dx10Metadata->pixelFormat.fourCc ==
                   std::array{ std::byte{ 'D' }, std::byte{ 'X' }, std::byte{ '1' }, std::byte{ '0' } },
               "DDS FourCC bytes are preserved");
        expect(dx10Metadata->dx10Header->dxgiFormat == 28 &&
                   dx10Metadata->dx10Header->resourceDimension == 3 &&
                   dx10Metadata->dx10Header->miscFlag == 4 &&
                   dx10Metadata->dx10Header->arraySize == 2 &&
                   dx10Metadata->dx10Header->miscFlags2 == 1,
               "DDS DX10 extension values are preserved");
    }

    expect(!ParseDdsMetadata(std::span(legacy).first(127)),
           "truncated DDS base metadata is rejected");

    auto invalidSignature      = legacy;
    invalidSignature[0]        = std::byte{ 0 };
    const auto signatureResult = ParseDdsMetadata(invalidSignature);
    expect(!signatureResult &&
               signatureResult.error() == DdsMetadataError::InvalidSignature,
           "invalid DDS signature is rejected");

    auto invalidHeaderSize = legacy;
    writeU32Le(invalidHeaderSize, 4, 123);
    const auto headerSizeResult = ParseDdsMetadata(invalidHeaderSize);
    expect(!headerSizeResult &&
               headerSizeResult.error() == DdsMetadataError::InvalidHeaderSize,
           "invalid DDS header size is rejected");

    auto invalidPixelFormatSize = legacy;
    writeU32Le(invalidPixelFormatSize, 76, 31);
    const auto pixelSizeResult = ParseDdsMetadata(invalidPixelFormatSize);
    expect(!pixelSizeResult &&
               pixelSizeResult.error() == DdsMetadataError::InvalidPixelFormatSize,
           "invalid DDS pixel format size is rejected");

    const auto truncatedDx10 = std::span(dx10).first(147);
    const auto dx10Result    = ParseDdsMetadata(truncatedDx10);
    expect(!dx10Result && dx10Result.error() == DdsMetadataError::Truncated,
           "truncated DDS DX10 extension is rejected");
}

void testDdsDocument()
{
    using rerevved::tools::DdsMetadataError;
    using rerevved::tools::DdsPreviewError;
    using rerevved::tools::LoadDdsDocument;
    using rerevved::tools::ParseDdsDocument;

    const std::array bgraPixels{
        std::byte{ 0x33 },
        std::byte{ 0x22 },
        std::byte{ 0x11 },
        std::byte{ 0x44 },
        std::byte{ 0xCC },
        std::byte{ 0xBB },
        std::byte{ 0xAA },
        std::byte{ 0xDD },
    };
    const auto bgra         = makePreviewDds(2,
                                             1,
                                             0x00FF0000U,
                                             0x0000FF00U,
                                             0x000000FFU,
                                             0xFF000000U,
                                             bgraPixels);
    const auto bgraDocument = ParseDdsDocument(bgra);
    expect(bgraDocument.has_value(), "32-bit BGRA DDS preview decoding succeeds");
    if (bgraDocument)
    {
        expect(bgraDocument->metadata.width == 2 && bgraDocument->metadata.height == 1,
               "DDS preview dimensions come from parsed metadata");
        expect(bgraDocument->rgba8 ==
                   std::vector<std::byte>{
                       std::byte{ 0x11 },
                       std::byte{ 0x22 },
                       std::byte{ 0x33 },
                       std::byte{ 0x44 },
                       std::byte{ 0xAA },
                       std::byte{ 0xBB },
                       std::byte{ 0xCC },
                       std::byte{ 0xDD },
                   },
               "DDS channel masks produce RGBA8 preview pixels");
    }

    const std::array rgbaPixels{
        std::byte{ 0x10 },
        std::byte{ 0x20 },
        std::byte{ 0x30 },
        std::byte{ 0x7F },
    };
    const auto rgb         = makePreviewDds(1,
                                            1,
                                            0x000000FFU,
                                            0x0000FF00U,
                                            0x00FF0000U,
                                            0,
                                            rgbaPixels);
    const auto rgbDocument = ParseDdsDocument(rgb);
    expect(rgbDocument &&
               rgbDocument->rgba8 ==
                   std::vector<std::byte>{
                       std::byte{ 0x10 },
                       std::byte{ 0x20 },
                       std::byte{ 0x30 },
                       std::byte{ 0xFF },
                   },
           "DDS RGB preview pixels receive opaque alpha");

    const auto truncatedMetadata = ParseDdsDocument(std::span(bgra).first(127));
    expect(!truncatedMetadata &&
               std::get<DdsMetadataError>(truncatedMetadata.error()) ==
                   DdsMetadataError::Truncated,
           "DDS document preserves metadata truncation errors");

    const auto truncatedPixels = ParseDdsDocument(std::span(bgra).first(bgra.size() - 1));
    expect(!truncatedPixels &&
               std::get<DdsPreviewError>(truncatedPixels.error()) ==
                   DdsPreviewError::TruncatedPixelData,
           "truncated DDS top-level pixels are rejected");

    auto       unsupportedDx10 = makeDds(true);
    const auto dx10Result      = ParseDdsDocument(unsupportedDx10);
    expect(!dx10Result &&
               std::get<DdsPreviewError>(dx10Result.error()) ==
                   DdsPreviewError::UnsupportedFormat,
           "unsupported DX10 preview encoding is rejected");

    auto unsupportedMasks = bgra;
    writeU32Le(unsupportedMasks, 92, 0x000003FFU);
    const auto maskResult = ParseDdsDocument(unsupportedMasks);
    expect(!maskResult &&
               std::get<DdsPreviewError>(maskResult.error()) ==
                   DdsPreviewError::UnsupportedFormat,
           "unsupported DDS channel masks are rejected");

    auto ambiguousFlags = bgra;
    writeU32Le(ambiguousFlags, 80, 0x00020041U);
    const auto flagResult = ParseDdsDocument(ambiguousFlags);
    expect(!flagResult &&
               std::get<DdsPreviewError>(flagResult.error()) ==
                   DdsPreviewError::UnsupportedFormat,
           "ambiguous DDS pixel format flags are rejected");

    auto cubemap = bgra;
    writeU32Le(cubemap, 112, 0x00000200U);
    const auto cubemapResult = ParseDdsDocument(cubemap);
    expect(!cubemapResult &&
               std::get<DdsPreviewError>(cubemapResult.error()) ==
                   DdsPreviewError::UnsupportedFormat,
           "DDS cubemaps are rejected by the 2D preview");

    auto invalidDimensions = bgra;
    writeU32Le(invalidDimensions, 16, 0);
    const auto dimensionResult = ParseDdsDocument(invalidDimensions);
    expect(!dimensionResult &&
               std::get<DdsPreviewError>(dimensionResult.error()) ==
                   DdsPreviewError::InvalidDimensions,
           "zero DDS preview dimensions are rejected");

    auto overflowingDimensions = bgra;
    writeU32Le(overflowingDimensions, 12, std::numeric_limits<std::uint32_t>::max());
    writeU32Le(overflowingDimensions, 16, std::numeric_limits<std::uint32_t>::max());
    const auto overflowResult = ParseDdsDocument(overflowingDimensions);
    expect(!overflowResult &&
               std::get<DdsPreviewError>(overflowResult.error()) ==
                   DdsPreviewError::SizeOverflow,
           "overflowing DDS preview dimensions are rejected");

    const auto path = std::filesystem::temp_directory_path() /
                      "rerevved-tools-dds-document-test.dds";
    {
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(bgra.data()),
                     static_cast<std::streamsize>(bgra.size()));
    }
    const auto loadedDocument = LoadDdsDocument(path);
    expect(loadedDocument && bgraDocument &&
               loadedDocument->rgba8 == bgraDocument->rgba8,
           "DDS document loader preserves the synthetic decoded preview");
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

void testGfxDocument()
{
    using rerevved::tools::GfxDocumentError;
    using rerevved::tools::LoadGfxDocument;
    using rerevved::tools::ParseGfxDocument;

    const auto bytes    = makeGfx();
    const auto document = ParseGfxDocument(bytes);
    expect(document.has_value(), "synthetic GFX v2 parsing succeeds");
    if (document)
    {
        expect(document->fileVersion == 8 && document->declaredLength == bytes.size(),
               "GFX file metadata is preserved");
        expect(document->frame.xMinTwips == -20 &&
                   document->frame.xMaxTwips == 1980 &&
                   document->frame.yMinTwips == -40 &&
                   document->frame.yMaxTwips == 960,
               "signed GFX frame coordinates are preserved");
        expect(document->frameRateRaw == 0x1800 && document->frameCount == 2,
               "GFX frame rate and count are preserved");
        expect(document->exporter.version == 0x0204 && document->exporter.flags == 5 &&
                   document->exporter.bitmapFormat == 2 &&
                   document->exporter.prefix == byteString("ui/") &&
                   document->exporter.swfName == byteString("menu"),
               "GFX exporter metadata is preserved");
        expect(document->externalImages.size() == 2,
               "long tags, unknown tags, and duplicate resources parse");
        if (document->externalImages.size() == 2)
        {
            const auto& image = document->externalImages[0];
            expect(image.characterId == 7 && image.bitmapFormat == 2 &&
                       image.targetWidth == 64 && image.targetHeight == 32,
                   "GFX external image numeric fields are preserved");
            expect(image.exportName ==
                           std::vector<std::byte>{
                               std::byte{ 'i' },
                               std::byte{ 0 },
                               std::byte{ 0xFF },
                           } &&
                       image.fileName == byteString("icon.dds") &&
                       document->externalImages[1].exportName == image.exportName &&
                       document->externalImages[1].fileName == image.fileName,
                   "GFX external names preserve bytes and source order");
        }
    }

    auto noResources = beginGfx();
    auto exporter    = makeExporterInfo(0x02FF);
    appendGfxTag(noResources, 1000, exporter);
    finishGfx(noResources);
    const auto emptyDocument = ParseGfxDocument(noResources);
    expect(emptyDocument && emptyDocument->externalImages.empty() &&
               emptyDocument->exporter.version == 0x02FF,
           "GFX 2.x without external resources succeeds");

    expect(rerevved::tools::FormatGfxBytes(
               std::array{ std::byte{ 'A' }, std::byte{ '\\' }, std::byte{ 0 }, std::byte{ 0xFF } }) ==
               "A\\\\\\x00\\xFF",
           "GFX name bytes receive safe app-local formatting");

    const std::array shortInput{ std::byte{ 'G' }, std::byte{ 'F' } };
    const auto       truncated = ParseGfxDocument(shortInput);
    expect(!truncated && truncated.error() == GfxDocumentError::Truncated,
           "truncated GFX signature is rejected");

    const std::array invalidSignature{
        std::byte{ 'F' },
        std::byte{ 'W' },
        std::byte{ 'S' },
    };
    const auto signatureResult = ParseGfxDocument(invalidSignature);
    expect(!signatureResult && signatureResult.error() == GfxDocumentError::InvalidSignature,
           "non-GFX signature is rejected");

    const std::array compressedSignature{
        std::byte{ 'C' },
        std::byte{ 'F' },
        std::byte{ 'X' },
    };
    const auto compressedResult = ParseGfxDocument(compressedSignature);
    expect(!compressedResult &&
               compressedResult.error() == GfxDocumentError::UnsupportedCompression,
           "compressed GFX signature is reported as unsupported");

    auto mismatchedLength = bytes;
    mismatchedLength.push_back(std::byte{ 0 });
    const auto lengthResult = ParseGfxDocument(mismatchedLength);
    expect(!lengthResult && lengthResult.error() == GfxDocumentError::LengthMismatch,
           "GFX declared length mismatch is rejected");

    auto missingExporter = beginGfx();
    finishGfx(missingExporter);
    const auto exporterResult = ParseGfxDocument(missingExporter);
    expect(!exporterResult &&
               exporterResult.error() == GfxDocumentError::MissingExporterInfo,
           "missing first GFX exporter tag is rejected");

    auto unsupportedVersion = beginGfx();
    exporter                = makeExporterInfo(0x0300);
    appendGfxTag(unsupportedVersion, 1000, exporter);
    finishGfx(unsupportedVersion);
    const auto versionResult = ParseGfxDocument(unsupportedVersion);
    expect(!versionResult &&
               versionResult.error() == GfxDocumentError::UnsupportedExporterVersion,
           "non-v2 GFX exporter is rejected");

    auto missingEnd = beginGfx();
    exporter        = makeExporterInfo();
    appendGfxTag(missingEnd, 1000, exporter);
    updateGfxLength(missingEnd);
    const auto endResult = ParseGfxDocument(missingEnd);
    expect(!endResult && endResult.error() == GfxDocumentError::InvalidTagStream,
           "missing GFX End tag is rejected");

    auto malformedEnd = beginGfx();
    appendGfxTag(malformedEnd, 1000, exporter);
    appendGfxTag(malformedEnd, 0, std::array{ std::byte{ 0 } });
    updateGfxLength(malformedEnd);
    expect(!ParseGfxDocument(malformedEnd), "non-empty GFX End tag is rejected");

    auto trailingAfterEnd = noResources;
    trailingAfterEnd.push_back(std::byte{ 0 });
    updateGfxLength(trailingAfterEnd);
    expect(!ParseGfxDocument(trailingAfterEnd), "bytes after GFX End tag are rejected");

    auto truncatedTag = beginGfx();
    appendGfxTag(truncatedTag, 1000, exporter);
    appendU16Le(truncatedTag, static_cast<std::uint16_t>((77U << 6U) | 5U));
    truncatedTag.push_back(std::byte{ 0xA5 });
    updateGfxLength(truncatedTag);
    const auto tagResult = ParseGfxDocument(truncatedTag);
    expect(!tagResult && tagResult.error() == GfxDocumentError::Truncated,
           "out-of-range GFX tag payload is rejected");

    auto truncatedLongHeader = beginGfx();
    appendGfxTag(truncatedLongHeader, 1000, exporter);
    appendU16Le(truncatedLongHeader, static_cast<std::uint16_t>((77U << 6U) | 0x3FU));
    updateGfxLength(truncatedLongHeader);
    const auto longHeaderResult = ParseGfxDocument(truncatedLongHeader);
    expect(!longHeaderResult && longHeaderResult.error() == GfxDocumentError::Truncated,
           "truncated GFX long tag header is rejected");

    auto truncatedRect = beginGfx();
    truncatedRect.resize(9);
    updateGfxLength(truncatedRect);
    const auto rectResult = ParseGfxDocument(truncatedRect);
    expect(!rectResult && rectResult.error() == GfxDocumentError::Truncated,
           "truncated GFX frame rectangle is rejected");

    auto                   badExporter = beginGfx();
    std::vector<std::byte> truncatedExporter;
    appendU16Le(truncatedExporter, 0x0204);
    appendU32Le(truncatedExporter, 0);
    appendU16Le(truncatedExporter, 0);
    truncatedExporter.push_back(std::byte{ 3 });
    truncatedExporter.push_back(std::byte{ 'x' });
    appendGfxTag(badExporter, 1000, truncatedExporter);
    finishGfx(badExporter);
    const auto badExporterResult = ParseGfxDocument(badExporter);
    expect(!badExporterResult && badExporterResult.error() == GfxDocumentError::Truncated,
           "truncated GFX exporter string is rejected");

    auto badImage = beginGfx();
    appendGfxTag(badImage, 1000, exporter);
    std::vector<std::byte> truncatedImage;
    appendU16Le(truncatedImage, 1);
    appendU16Le(truncatedImage, 2);
    appendU16Le(truncatedImage, 1);
    appendU16Le(truncatedImage, 1);
    truncatedImage.push_back(std::byte{ 0 });
    truncatedImage.push_back(std::byte{ 3 });
    truncatedImage.push_back(std::byte{ 'x' });
    appendGfxTag(badImage, 1001, truncatedImage);
    finishGfx(badImage);
    const auto badImageResult = ParseGfxDocument(badImage);
    expect(!badImageResult && badImageResult.error() == GfxDocumentError::Truncated,
           "truncated GFX external filename is rejected");

    const auto path = std::filesystem::temp_directory_path() /
                      "rerevved-tools-gfx-document-test.gfx";
    {
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }
    const auto loadedDocument = LoadGfxDocument(path);
    expect(loadedDocument && loadedDocument->externalImages.size() == 2,
           "GFX document loader preserves synthetic parse results");

    const auto invalidPath = std::filesystem::temp_directory_path() /
                             "rerevved-tools-invalid-gfx-document-test.gfx";
    {
        std::ofstream output(invalidPath, std::ios::binary);
        output.write("FWS", 3);
    }
    const auto invalidDocument = LoadGfxDocument(invalidPath);
    expect(!invalidDocument &&
               invalidDocument.error() == "The file does not have a valid GFX signature.",
           "GFX loader maps parse failures to app-facing messages");
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(invalidPath, ignored);
}

void testMapDocument()
{
    using rerevved::tools::kMapCoreSize;
    using rerevved::tools::kMapEnvelopeSize;
    using rerevved::tools::kMapFooterSize;
    using rerevved::tools::kMapHeight;
    using rerevved::tools::kMapWidth;
    using rerevved::tools::LoadMapDocument;
    using rerevved::tools::MapDocumentError;
    using rerevved::tools::ParseMapDocument;

    expect(kMapWidth == 32 && kMapHeight == 32 && kMapCoreSize == 1024 &&
               kMapFooterSize == 64 && kMapEnvelopeSize == 1088,
           "Xbox DLC map envelope dimensions are fixed");

    std::array<std::byte, kMapEnvelopeSize> bytes{};
    for (std::size_t offset = 0; offset < kMapCoreSize; ++offset)
    {
        bytes[offset] = static_cast<std::byte>((offset * 37U + 11U) & 0xFFU);
    }
    std::ranges::fill(bytes.begin() + static_cast<std::ptrdiff_t>(kMapCoreSize),
                      bytes.end(),
                      std::byte{ 0xFF });

    const auto document = ParseMapDocument(bytes);
    expect(document.has_value(), "synthetic 1088-byte map parsing succeeds");
    if (document)
    {
        expect(std::ranges::equal(document->core, std::span(bytes).first(kMapCoreSize)),
               "map parser preserves the 1024-byte core");
        expect(std::ranges::equal(document->footer,
                                  std::span(bytes).last(kMapFooterSize)),
               "map parser preserves the 64-byte footer");
    }

    auto unusualFooter              = bytes;
    unusualFooter[kMapCoreSize + 7] = std::byte{ 0x2A };
    const auto unusualDocument      = ParseMapDocument(unusualFooter);
    expect(unusualDocument && unusualDocument->footer[7] == std::byte{ 0x2A },
           "unproved map footer values remain observable");

    const auto truncated = ParseMapDocument(std::span(bytes).first(kMapEnvelopeSize - 1));
    expect(!truncated && truncated.error() == MapDocumentError::Truncated,
           "truncated map envelope is rejected");
    const auto empty = ParseMapDocument({});
    expect(!empty && empty.error() == MapDocumentError::Truncated,
           "empty map envelope is rejected");

    std::vector<std::byte> oversized(bytes.begin(), bytes.end());
    oversized.push_back(std::byte{ 0 });
    const auto unsupported = ParseMapDocument(oversized);
    expect(!unsupported && unsupported.error() == MapDocumentError::UnsupportedLength,
           "map envelope with trailing data is rejected");

    const auto path = std::filesystem::temp_directory_path() /
                      "rerevved-tools-map-document-test.map";
    {
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }
    const auto loaded = LoadMapDocument(path);
    expect(loaded &&
               std::ranges::equal(loaded->core, std::span(bytes).first(kMapCoreSize)),
           "map loader preserves the synthetic document");

    const auto truncatedPath = std::filesystem::temp_directory_path() /
                               "rerevved-tools-truncated-map-document-test.map";
    {
        std::ofstream output(truncatedPath, std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size() - 1));
    }
    const auto truncatedLoad = LoadMapDocument(truncatedPath);
    expect(!truncatedLoad &&
               truncatedLoad.error() ==
                   "The map record is truncated. The Xbox DLC profile requires 1088 bytes.",
           "map loader maps truncated input to an app-facing message");

    const auto oversizedPath = std::filesystem::temp_directory_path() /
                               "rerevved-tools-oversized-map-document-test.map";
    {
        std::ofstream output(oversizedPath, std::ios::binary);
        output.write(reinterpret_cast<const char*>(oversized.data()),
                     static_cast<std::streamsize>(oversized.size()));
    }
    const auto oversizedLoad = LoadMapDocument(oversizedPath);
    expect(!oversizedLoad &&
               oversizedLoad.error() ==
                   "The map record length is unsupported. The Xbox DLC profile requires exactly 1088 bytes.",
           "map loader maps unsupported length to an app-facing message");

    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(truncatedPath, ignored);
    std::filesystem::remove(oversizedPath, ignored);
}

void testMp3Document()
{
    using rerevved::tools::LoadMp3Document;
    using rerevved::tools::Mp3DocumentError;
    using rerevved::tools::ParseMp3Document;
    using rerevved::tools::test::kSyntheticMp3;

    expect(kSyntheticMp3.size() == 418, "synthetic MP3 byte count is preserved");
    const auto bytes =
        std::as_bytes(std::span(kSyntheticMp3.data(), kSyntheticMp3.size()));
    const auto document = ParseMp3Document(bytes);
    expect(document.has_value(), "synthetic MP3 waveform decoding succeeds");
    if (document)
    {
        expect(document->sampleRate == 11025 && document->channels == 1,
               "MP3 stream metadata is preserved");
        expect(document->previewFrameCount > 0 && document->previewComplete,
               "short MP3 preview reaches the stream boundary");
        expect(!document->waveform.empty(), "MP3 preview contains waveform points");
        expect(std::ranges::all_of(document->waveform,
                                   [](float value)
                                   {
                                       return value >= -1.0F && value <= 1.0F;
                                   }),
               "MP3 waveform points stay in the decoded amplitude range");
    }

    const std::array<unsigned char, 2> truncated{ 0xFF, 0xE3 };
    const auto                         truncatedResult = ParseMp3Document(std::as_bytes(std::span(truncated)));
    expect(!truncatedResult && truncatedResult.error() == Mp3DocumentError::DecodeFailed,
           "truncated MP3 data reports a clean decode failure");
    const auto emptyResult = ParseMp3Document({});
    expect(!emptyResult && emptyResult.error() == Mp3DocumentError::DecodeFailed,
           "empty MP3 data reports a clean decode failure");
    const auto damagedTail = ParseMp3Document(bytes.first(bytes.size() - 20));
    expect(!damagedTail && damagedTail.error() == Mp3DocumentError::DecodeFailed,
           "MP3 data with a valid prefix and truncated final frame is rejected");

    std::vector<char> longStream;
    for (int repetition = 0; repetition < 60; ++repetition)
    {
        longStream.insert(longStream.end(), kSyntheticMp3.begin(), kSyntheticMp3.end());
    }
    const auto bounded = ParseMp3Document(std::as_bytes(std::span(longStream)));
    expect(bounded && bounded->previewFrameCount == bounded->sampleRate * 10ULL &&
               !bounded->previewComplete && bounded->waveform.size() == 1000,
           "MP3 waveform decoding stops at the ten-second preview boundary");

    const auto path = std::filesystem::temp_directory_path() /
                      "rerevved-tools-mp3-document-test.mp3";
    {
        std::ofstream output(path, std::ios::binary);
        output.write(kSyntheticMp3.data(), static_cast<std::streamsize>(kSyntheticMp3.size()));
    }
    const auto loaded = LoadMp3Document(path);
    expect(loaded && loaded->sampleRate == 11025,
           "MP3 loader decodes the synthetic stream");

    const auto invalidPath = std::filesystem::temp_directory_path() /
                             "rerevved-tools-invalid-mp3-document-test.mp3";
    {
        std::ofstream output(invalidPath, std::ios::binary);
        output.write("not mp3", 7);
    }
    const auto invalid = LoadMp3Document(invalidPath);
    expect(!invalid && invalid.error() == "The MP3 audio could not be decoded.",
           "MP3 loader maps decoder failures to an app-facing message");
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(invalidPath, ignored);
}

void testNifDocument()
{
    using rerevved::tools::LoadNifDocument;
    using rerevved::tools::NifDocumentError;
    using rerevved::tools::ParseNifDocument;

    const auto synthetic = makeNif();
    const auto document  = ParseNifDocument(synthetic.bytes);
    expect(document.has_value(), "synthetic big-endian NIF parsing succeeds");
    if (document)
    {
        expect(document->version == 0x14030009 && document->endian == 0 &&
                   document->userVersion == 0,
               "NIF version fields are preserved");
        expect(document->blockTypes.size() == 3 &&
                   document->blockTypes[0] == byteString("NiNode") &&
                   document->blockTypes[1] == byteString("NiTriShape") &&
                   document->blockTypes[2] == byteString("OpaqueBlock"),
               "NIF block type bytes are preserved");
        expect(document->blocks.size() == 3 && document->blocks[0].typeIndex == 0 &&
                   document->blocks[0].size == 94 && document->blocks[1].typeIndex == 1 &&
                   document->blocks[1].size == 107 && document->blocks[2].size == 3,
               "NIF block inventory is preserved");
        expect(document->blocks[0].offset == document->headerSize &&
                   document->blocks[1].offset == document->headerSize + 94 &&
                   document->blocks[2].offset == document->headerSize + 201,
               "NIF block offsets are derived from declared sizes");
        expect(document->strings.size() == 2 && document->strings[0] == byteString("Root") &&
                   document->strings[1] ==
                       std::vector<std::byte>{ std::byte{ 0 }, std::byte{ 0xFF } } &&
                   document->maxStringLength == 4,
               "NIF strings preserve non-text bytes and maximum length");
        expect(document->groups == std::vector<std::uint32_t>{ 3 } &&
                   document->roots == std::vector<std::uint32_t>{ 0 },
               "NIF groups and roots are preserved");
        expect(document->nodes.size() == 1 && document->nodes[0].blockIndex == 0 &&
                   document->nodes[0].object.nameIndex == 0 &&
                   document->nodes[0].object.extraData == std::vector<std::uint32_t>{ 2 } &&
                   document->nodes[0].object.flags == 0x1234 &&
                   document->nodes[0].object.translation == std::array{ 1.0F, 2.0F, 3.0F } &&
                   document->nodes[0].object.scale == 2.0F &&
                   document->nodes[0].object.properties == std::vector<std::uint32_t>{ 2 } &&
                   document->nodes[0].children == std::vector<std::uint32_t>{ 1 } &&
                   document->nodes[0].effects.empty(),
               "standard NiNode metadata and references are preserved");
        expect(document->triShapes.size() == 1 && document->triShapes[0].blockIndex == 1 &&
                   document->triShapes[0].object.nameIndex == 1 &&
                   document->triShapes[0].data == 2 &&
                   document->triShapes[0].skinInstance == 0xFFFFFFFF &&
                   document->triShapes[0].material.nameIndices ==
                       std::vector<std::uint32_t>{ 0 } &&
                   document->triShapes[0].material.extraData ==
                       std::vector<std::int32_t>{ -7 } &&
                   document->triShapes[0].material.activeMaterial == 0 &&
                   document->triShapes[0].material.materialNeedsUpdate == 1,
               "standard NiTriShape metadata and material fields are preserved");
    }

    bool allTruncationsRejected = true;
    for (std::size_t size = 0; size < synthetic.bytes.size(); ++size)
    {
        allTruncationsRejected =
            allTruncationsRejected && !ParseNifDocument(std::span(synthetic.bytes).first(size));
    }
    expect(allTruncationsRejected, "every truncated NIF prefix is rejected");

    auto invalidSignature      = synthetic.bytes;
    invalidSignature[0]        = std::byte{ 'N' };
    const auto signatureResult = ParseNifDocument(invalidSignature);
    expect(!signatureResult && signatureResult.error() == NifDocumentError::InvalidSignature,
           "non-Gamebryo NIF signature is rejected");

    auto unsupportedTextVersion  = synthetic.bytes;
    unsupportedTextVersion[37]   = std::byte{ '8' };
    const auto textVersionResult = ParseNifDocument(unsupportedTextVersion);
    expect(!textVersionResult &&
               textVersionResult.error() == NifDocumentError::UnsupportedVersion,
           "unsupported textual NIF version is rejected");

    auto unsupportedBinaryVersion = synthetic.bytes;
    writeU32Le(unsupportedBinaryVersion, 39, 0x14020007);
    const auto binaryVersionResult = ParseNifDocument(unsupportedBinaryVersion);
    expect(!binaryVersionResult &&
               binaryVersionResult.error() == NifDocumentError::UnsupportedVersion,
           "unsupported binary NIF version is rejected");

    auto unsupportedEndian  = synthetic.bytes;
    unsupportedEndian[43]   = std::byte{ 1 };
    const auto endianResult = ParseNifDocument(unsupportedEndian);
    expect(!endianResult && endianResult.error() == NifDocumentError::UnsupportedEndian,
           "unobserved little-endian NIF layout is rejected");

    auto unsupportedUserVersion = synthetic.bytes;
    writeU32Le(unsupportedUserVersion, 44, 1);
    const auto userVersionResult = ParseNifDocument(unsupportedUserVersion);
    expect(!userVersionResult &&
               userVersionResult.error() == NifDocumentError::UnsupportedUserVersion,
           "unobserved NIF user version is rejected");

    auto invalidTypeIndex = synthetic.bytes;
    writeU16Be(invalidTypeIndex, synthetic.firstTypeIndexOffset, 3);
    const auto typeIndexResult = ParseNifDocument(invalidTypeIndex);
    expect(!typeIndexResult && typeIndexResult.error() == NifDocumentError::InvalidLayout,
           "out-of-range NIF block type index is rejected");

    auto invalidNodeName = synthetic.bytes;
    writeU32Be(invalidNodeName, synthetic.nodeNameIndexOffset, 2);
    const auto nodeNameResult = ParseNifDocument(invalidNodeName);
    expect(!nodeNameResult && nodeNameResult.error() == NifDocumentError::InvalidLayout,
           "out-of-range NiNode name index is rejected");

    auto nullNodeName = synthetic.bytes;
    writeU32Be(nullNodeName, synthetic.nodeNameIndexOffset, 0xFFFFFFFF);
    const auto nullNodeNameResult = ParseNifDocument(nullNodeName);
    expect(nullNodeNameResult && nullNodeNameResult->nodes[0].object.nameIndex == 0xFFFFFFFF,
           "observed null NiNode name index is preserved");

    auto oversizedExtraData = synthetic.bytes;
    writeU32Be(oversizedExtraData, synthetic.nodeExtraCountOffset, 0xFFFFFFFF);
    const auto extraDataResult = ParseNifDocument(oversizedExtraData);
    expect(!extraDataResult && extraDataResult.error() == NifDocumentError::InvalidLayout,
           "oversized NiNode extra-data list is rejected within its block");

    auto invalidChild = synthetic.bytes;
    writeU32Be(invalidChild, synthetic.nodeChildOffset, 3);
    const auto childResult = ParseNifDocument(invalidChild);
    expect(!childResult && childResult.error() == NifDocumentError::InvalidLayout,
           "out-of-range NiNode child reference is rejected");

    auto invalidShapeData = synthetic.bytes;
    writeU32Be(invalidShapeData, synthetic.shapeDataOffset, 3);
    const auto shapeDataResult = ParseNifDocument(invalidShapeData);
    expect(!shapeDataResult && shapeDataResult.error() == NifDocumentError::InvalidLayout,
           "out-of-range NiTriShape data reference is rejected");

    auto oversizedMaterials = synthetic.bytes;
    writeU32Be(oversizedMaterials, synthetic.shapeMaterialCountOffset, 0xFFFFFFFF);
    const auto materialsResult = ParseNifDocument(oversizedMaterials);
    expect(!materialsResult && materialsResult.error() == NifDocumentError::InvalidLayout,
           "oversized NiTriShape material list is rejected within its block");

    auto invalidMaterialName = synthetic.bytes;
    writeU32Be(invalidMaterialName, synthetic.shapeMaterialCountOffset + 4, 2);
    const auto materialNameResult = ParseNifDocument(invalidMaterialName);
    expect(!materialNameResult &&
               materialNameResult.error() == NifDocumentError::InvalidLayout,
           "out-of-range NiTriShape material name index is rejected");

    auto truncatedNodeLayout = synthetic.bytes;
    writeU32Be(truncatedNodeLayout, synthetic.nodeChildOffset + 4, 1);
    const auto nodeLayoutResult = ParseNifDocument(truncatedNodeLayout);
    expect(!nodeLayoutResult && nodeLayoutResult.error() == NifDocumentError::InvalidLayout,
           "NiNode arrays must fit exactly inside the declared block");

    auto oversizedBlockCount = synthetic.bytes;
    writeU32Le(oversizedBlockCount, 48, 0xFFFFFFFF);
    const auto blockCountResult = ParseNifDocument(oversizedBlockCount);
    expect(!blockCountResult && blockCountResult.error() == NifDocumentError::Truncated,
           "oversized NIF block count is rejected before allocation");

    auto oversizedTypeCount = synthetic.bytes;
    writeU16Be(oversizedTypeCount, synthetic.blockTypeCountOffset, 0xFFFF);
    const auto typeCountResult = ParseNifDocument(oversizedTypeCount);
    expect(!typeCountResult && typeCountResult.error() == NifDocumentError::Truncated,
           "oversized NIF block type count is rejected before allocation");

    const auto truncatedType = ParseNifDocument(std::span(synthetic.bytes).first(60));
    expect(!truncatedType && truncatedType.error() == NifDocumentError::Truncated,
           "truncated NIF type string is rejected");

    auto oversizedBlock = synthetic.bytes;
    writeU32Be(oversizedBlock, synthetic.firstBlockSizeOffset, 0xFFFFFFFF);
    const auto blockResult = ParseNifDocument(oversizedBlock);
    expect(!blockResult && blockResult.error() == NifDocumentError::Truncated,
           "out-of-range NIF block payload is rejected");

    auto invalidMaxString = synthetic.bytes;
    writeU32Be(invalidMaxString, synthetic.maxStringLengthOffset, 1);
    const auto stringResult = ParseNifDocument(invalidMaxString);
    expect(!stringResult && stringResult.error() == NifDocumentError::InvalidLayout,
           "NIF string longer than declared maximum is rejected");

    auto oversizedStringCount = synthetic.bytes;
    writeU32Be(oversizedStringCount, synthetic.stringCountOffset, 0xFFFFFFFF);
    const auto stringCountResult = ParseNifDocument(oversizedStringCount);
    expect(!stringCountResult && stringCountResult.error() == NifDocumentError::Truncated,
           "oversized NIF string count is rejected before allocation");

    auto oversizedGroupCount = synthetic.bytes;
    writeU32Be(oversizedGroupCount, synthetic.groupCountOffset, 0xFFFFFFFF);
    const auto groupCountResult = ParseNifDocument(oversizedGroupCount);
    expect(!groupCountResult && groupCountResult.error() == NifDocumentError::Truncated,
           "oversized NIF group count is rejected before allocation");

    auto truncatedFooter = synthetic.bytes;
    truncatedFooter.pop_back();
    const auto footerResult = ParseNifDocument(truncatedFooter);
    expect(!footerResult && footerResult.error() == NifDocumentError::Truncated,
           "truncated NIF footer is rejected");

    auto oversizedRootCount = synthetic.bytes;
    writeU32Be(oversizedRootCount, synthetic.rootCountOffset, 0xFFFFFFFF);
    const auto rootCountResult = ParseNifDocument(oversizedRootCount);
    expect(!rootCountResult && rootCountResult.error() == NifDocumentError::Truncated,
           "oversized NIF root count is rejected before allocation");

    auto trailingBytes = synthetic.bytes;
    trailingBytes.push_back(std::byte{ 0 });
    const auto trailingResult = ParseNifDocument(trailingBytes);
    expect(!trailingResult && trailingResult.error() == NifDocumentError::InvalidLayout,
           "bytes after the NIF footer are rejected");

    auto invalidRoot = synthetic.bytes;
    writeU32Be(invalidRoot, synthetic.rootIndexOffset, 3);
    const auto rootResult = ParseNifDocument(invalidRoot);
    expect(!rootResult && rootResult.error() == NifDocumentError::InvalidLayout,
           "out-of-range NIF root reference is rejected");

    auto nullRoot = synthetic.bytes;
    writeU32Be(nullRoot, synthetic.rootIndexOffset, 0xFFFFFFFF);
    const auto nullRootResult = ParseNifDocument(nullRoot);
    expect(nullRootResult && nullRootResult->roots[0] == 0xFFFFFFFF,
           "NIF null root reference is preserved");

    const auto path = std::filesystem::temp_directory_path() /
                      "rerevved-tools-nif-document-test.nif";
    {
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(synthetic.bytes.data()),
                     static_cast<std::streamsize>(synthetic.bytes.size()));
    }
    const auto loadedDocument = LoadNifDocument(path);
    expect(loadedDocument && loadedDocument->blocks.size() == 3,
           "NIF document loader preserves the synthetic block inventory");

    const auto invalidPath = std::filesystem::temp_directory_path() /
                             "rerevved-tools-invalid-nif-document-test.nif";
    {
        std::ofstream output(invalidPath, std::ios::binary);
        output.write("Not a NIF file", 14);
    }
    const auto invalidDocument = LoadNifDocument(invalidPath);
    expect(!invalidDocument && invalidDocument.error() == "The NIF file is truncated.",
           "NIF loader maps parse failures to app-facing messages");
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(invalidPath, ignored);
}

void testNifGeometryInventory()
{
    using rerevved::tools::NifDocumentError;
    using rerevved::tools::ParseNifDocument;

    const auto populated = makeGeometryNif(true);
    const auto document  = ParseNifDocument(populated.bytes);
    expect(document && document->triShapeData.size() == 1,
           "synthetic NiTriShapeData inventory parsing succeeds");
    if (document && document->triShapeData.size() == 1)
    {
        const auto& data = document->triShapeData[0];
        expect(data.blockIndex == 0 && data.groupId == -7 && data.vertexCount == 2 &&
                   data.keepFlags == 0xA1 && data.compressFlags == 0xB2 &&
                   data.hasVertices == 0xFF && data.dataFlags == 0x1001 &&
                   data.hasNormals == 2,
               "NiTriShapeData vertex inventory and raw presence values are preserved");
        expect(data.vertexPositions ==
                       std::vector<std::array<float, 3>>(2, std::array{ 0.0F, 0.0F, 0.0F }) &&
                   data.normalVectors ==
                       std::vector<std::array<float, 3>>(2, std::array{ 0.0F, 0.0F, 0.0F }),
               "NiTriShapeData positions and normals are retained in source order");
        expect(data.boundCenter == std::array{ 1.0F, 2.0F, 3.0F } &&
                   data.boundRadius == -4.0F && data.hasVertexColors == 0x80 &&
                   data.consistencyFlags == 0xBEEF && data.additionalData == 0xFFFFFFFF,
               "NiTriShapeData bounds, flags, and additional-data reference are preserved");
        expect(data.triangleCount == 1 && data.trianglePointCount == 7 &&
                   data.hasTriangles == 0x7F && data.matchGroupCount == 2,
               "NiTriShapeData triangle inventory is preserved without semantic rejection");
        expect(data.triangles ==
                       std::vector<std::array<std::uint16_t, 3>>{ { 0, 1, 0 } } &&
                   data.normalSharingGroups ==
                       std::vector<std::vector<std::uint16_t>>{ { 0, 7 }, { 5, 1, 9 } },
               "triangle and above-vertex normal-sharing selectors remain ordered and exact");
    }

    const auto empty       = makeGeometryNif(false);
    const auto emptyResult = ParseNifDocument(empty.bytes);
    expect(emptyResult && emptyResult->triShapeData.size() == 1 &&
               emptyResult->triShapeData[0].vertexCount == 0 &&
               emptyResult->triShapeData[0].triangleCount == 0 &&
               emptyResult->triShapeData[0].matchGroupCount == 0 &&
               emptyResult->triShapeData[0].vertexPositions.empty() &&
               emptyResult->triShapeData[0].normalVectors.empty() &&
               emptyResult->triShapeData[0].triangles.empty() &&
               emptyResult->triShapeData[0].normalSharingGroups.empty(),
           "absent NiTriShapeData arrays produce empty retained containers");

    auto tangentFlagWithoutNormals = empty.bytes;
    writeU16Be(tangentFlagWithoutNormals, empty.dataFlagsOffset, 0x1000);
    const auto tangentFlagResult = ParseNifDocument(tangentFlagWithoutNormals);
    expect(tangentFlagResult && tangentFlagResult->triShapeData.size() == 1 &&
               tangentFlagResult->triShapeData[0].hasNormals == 0 &&
               tangentFlagResult->triShapeData[0].normalVectors.empty(),
           "NiTriShapeData tangent flag does not create retained arrays without normals");

    bool allTruncationsRejected = true;
    for (std::size_t size = 0; size < populated.bytes.size(); ++size)
    {
        allTruncationsRejected =
            allTruncationsRejected && !ParseNifDocument(std::span(populated.bytes).first(size));
    }
    expect(allTruncationsRejected,
           "every truncated NiTriShapeData container prefix is rejected");

    for (const auto [arrayOffset, retainedBytes, label] :
         std::array{
             std::tuple{ populated.vertexPositionsOffset, std::size_t{ 23 }, "positions" },
             std::tuple{ populated.normalVectorsOffset, std::size_t{ 23 }, "normals" },
             std::tuple{ populated.triangleIndicesOffset, std::size_t{ 5 }, "triangles" },
             std::tuple{ populated.matchGroupSelectorsOffset,
                         std::size_t{ 3 },
                         "normal-sharing selectors" },
         })
    {
        auto truncatedArray = populated.bytes;
        writeU32Be(truncatedArray,
                   populated.blockSizeOffset,
                   static_cast<std::uint32_t>(arrayOffset - populated.blockPayloadOffset +
                                              retainedBytes));
        const auto result = ParseNifDocument(truncatedArray);
        expect(!result && result.error() == NifDocumentError::InvalidLayout,
               std::string("truncated NiTriShapeData ") + label + " are rejected safely");
    }

    auto oversizedVertices = populated.bytes;
    writeU16Be(oversizedVertices, populated.vertexCountOffset, 0xFFFF);
    const auto verticesResult = ParseNifDocument(oversizedVertices);
    expect(!verticesResult && verticesResult.error() == NifDocumentError::InvalidLayout,
           "oversized NiTriShapeData vertex arrays are rejected before retention allocation");

    auto oversizedNormals = empty.bytes;
    writeU16Be(oversizedNormals, empty.vertexCountOffset, 0xFFFF);
    oversizedNormals[empty.hasNormalsOffset] = std::byte{ 1 };
    const auto normalsResult                 = ParseNifDocument(oversizedNormals);
    expect(!normalsResult && normalsResult.error() == NifDocumentError::InvalidLayout,
           "oversized NiTriShapeData normal arrays are rejected before retention allocation");

    auto oversizedUvSets = populated.bytes;
    writeU16Be(oversizedUvSets, populated.dataFlagsOffset, 0x103F);
    const auto uvResult = ParseNifDocument(oversizedUvSets);
    expect(!uvResult && uvResult.error() == NifDocumentError::InvalidLayout,
           "oversized NiTriShapeData UV arrays are rejected within the block");

    auto invalidAdditionalData = populated.bytes;
    writeU32Be(invalidAdditionalData, populated.additionalDataOffset, 1);
    const auto additionalDataResult = ParseNifDocument(invalidAdditionalData);
    expect(!additionalDataResult &&
               additionalDataResult.error() == NifDocumentError::InvalidLayout,
           "out-of-range NiTriShapeData additional-data reference is rejected");

    auto oversizedTriangles = populated.bytes;
    writeU16Be(oversizedTriangles, populated.triangleCountOffset, 0xFFFF);
    const auto trianglesResult = ParseNifDocument(oversizedTriangles);
    expect(!trianglesResult && trianglesResult.error() == NifDocumentError::InvalidLayout,
           "oversized NiTriShapeData triangle arrays are rejected before retention allocation");

    auto oversizedMatchGroup = populated.bytes;
    writeU16Be(
        oversizedMatchGroup, populated.matchGroupVertexCountOffset, 0xFFFF);
    const auto matchGroupResult = ParseNifDocument(oversizedMatchGroup);
    expect(!matchGroupResult &&
               matchGroupResult.error() == NifDocumentError::InvalidLayout,
           "oversized normal-sharing selector arrays are rejected before retention allocation");

    auto trailingMatchGroup = populated.bytes;
    writeU16Be(trailingMatchGroup, populated.matchGroupCountOffset, 0);
    const auto trailingResult = ParseNifDocument(trailingMatchGroup);
    expect(!trailingResult && trailingResult.error() == NifDocumentError::InvalidLayout,
           "NiTriShapeData must consume its declared block exactly");
}

void testSyntheticModelFixture()
{
    using rerevved::tools::NifDocumentError;
    using rerevved::tools::ParseNifDocument;

    const auto synthetic = makeSyntheticModelNif();
    expect(synthetic.bytes == makeSyntheticModelNif().bytes,
           "synthetic model fixture generation is deterministic");

    const auto document = ParseNifDocument(synthetic.bytes);
    expect(document.has_value(), "complete synthetic model fixture parses successfully");
    if (document)
    {
        const std::vector<std::vector<std::byte>> expectedTypes{
            byteString("NiNode"),
            byteString("NiTriShape"),
            byteString("NiTriShapeData"),
            byteString("NiMaterialProperty"),
        };
        const std::vector<std::vector<std::byte>> expectedStrings{
            byteString("SyntheticRoot"),
            byteString("SyntheticTriangle"),
            byteString("SyntheticMaterial"),
        };
        expect(document->version == 0x14030009 && document->endian == 0 &&
                   document->userVersion == 0,
               "synthetic model fixture uses the supported Gamebryo profile");
        expect(document->blockTypes == expectedTypes && document->blocks.size() == 4 &&
                   document->blocks[0].typeIndex == 0 &&
                   document->blocks[1].typeIndex == 1 &&
                   document->blocks[2].typeIndex == 2 &&
                   document->blocks[3].typeIndex == 3,
               "synthetic model block types and type indices are exact and in range");
        expect(document->strings == expectedStrings && document->maxStringLength == 17 &&
                   document->groups.empty(),
               "synthetic model names and empty group table are exact");

        expect(document->roots == std::vector<std::uint32_t>{ 0 } &&
                   document->blocks[document->roots[0]].typeIndex == 0 &&
                   document->nodes.size() == 1 && document->nodes[0].blockIndex == 0,
               "synthetic footer root selects the NiNode block");
        expect(document->nodes[0].children == std::vector<std::uint32_t>{ 1 } &&
                   document->blocks[document->nodes[0].children[0]].typeIndex == 1,
               "synthetic NiNode child selects the NiTriShape block");
        expect(document->nodes[0].object.properties.empty() &&
                   document->nodes[0].object.controller == 0xFFFFFFFF &&
                   document->nodes[0].object.translation ==
                       std::array{ 0.0F, 0.0F, 0.0F } &&
                   document->nodes[0].object.rotation ==
                       std::array{ 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F } &&
                   document->nodes[0].object.scale == 1.0F &&
                   document->nodes[0].effects.empty(),
               "synthetic root retains its identity local transform and empty references");

        expect(document->triShapes.size() == 1 &&
                   document->triShapes[0].blockIndex == 1 &&
                   document->triShapes[0].data == 2 &&
                   document->blocks[document->triShapes[0].data].typeIndex == 2,
               "synthetic NiTriShape data reference selects NiTriShapeData");
        expect(document->triShapes[0].object.properties ==
                       std::vector<std::uint32_t>{ 3 } &&
                   document->blocks[document->triShapes[0].object.properties[0]].typeIndex ==
                       3 &&
                   document->blocks[3].size == 68,
               "synthetic NiTriShape property selects the standard NiMaterialProperty block");
        expect(document->materialProperties.size() == 1 &&
                   document->materialProperties[0].blockIndex == 3 &&
                   document->materialProperties[0].ambientColor ==
                       std::array{ 1.0F, 1.0F, 1.0F } &&
                   document->materialProperties[0].diffuseColor ==
                       std::array{ 1.0F, 1.0F, 1.0F } &&
                   document->materialProperties[0].specularColor ==
                       std::array{ 0.0F, 0.0F, 0.0F } &&
                   document->materialProperties[0].emissiveColor ==
                       std::array{ 0.0F, 0.0F, 0.0F } &&
                   document->materialProperties[0].glossiness == 1.0F &&
                   document->materialProperties[0].alpha == 1.0F,
               "material-positive fixture retains the complete material payload");
        expect(document->triShapes[0].object.controller == 0xFFFFFFFF &&
                   document->triShapes[0].object.translation ==
                       std::array{ 0.0F, 0.0F, 0.0F } &&
                   document->triShapes[0].object.rotation ==
                       std::array{ 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F } &&
                   document->triShapes[0].object.scale == 1.0F &&
                   document->triShapes[0].skinInstance == 0xFFFFFFFF &&
                   document->triShapes[0].material.nameIndices.empty() &&
                   document->triShapes[0].material.activeMaterial == -1,
               "synthetic shape has no animation, skinning, or material-name list");

        expect(document->triShapeData.size() == 1 &&
                   document->triShapeData[0].blockIndex == 2 &&
                   document->triShapeData[0].vertexCount == 3 &&
                   document->triShapeData[0].hasVertices == 1 &&
                   document->triShapeData[0].dataFlags == 0 &&
                   document->triShapeData[0].hasNormals == 1 &&
                   document->triShapeData[0].hasVertexColors == 0,
               "synthetic vertex and normal presence values are retained without UVs or colors");
        const std::vector<std::array<float, 3>> expectedPositions{
            { 0.0F, 0.0F, 0.0F },
            { 1.0F, 0.0F, 0.0F },
            { 0.0F, 1.0F, 0.0F },
        };
        const std::vector<std::array<float, 3>> expectedNormals{
            { 1.0F, 0.0F, 0.0F },
            { 0.0F, 1.0F, 0.0F },
            { 0.0F, 0.0F, 1.0F },
        };
        expect(document->triShapeData[0].vertexPositions == expectedPositions &&
                   document->triShapeData[0].normalVectors == expectedNormals,
               "synthetic vertex positions and normals retain their exact source order");
        expect(document->triShapeData[0].triangleCount == 1 &&
                   document->triShapeData[0].trianglePointCount == 3 &&
                   document->triShapeData[0].hasTriangles == 1 &&
                   document->triShapeData[0].matchGroupCount == 0,
               "synthetic triangle presence and counts are retained without match groups");
        expect(document->triShapeData[0].triangles ==
                       std::vector<std::array<std::uint16_t, 3>>{ { 0, 1, 2 } } &&
                   document->triShapeData[0].normalSharingGroups.empty(),
               "synthetic triangle selectors are retained and absent groups remain empty");
    }

    auto                 serializedTransform = synthetic.bytes;
    constexpr std::array kSerializedRotation{
        0.0F,
        1.0F,
        0.0F,
        -1.0F,
        0.0F,
        0.0F,
        0.0F,
        0.0F,
        1.0F,
    };
    writeNifF32(serializedTransform, synthetic.shapeTransformOffset, 4.0F);
    writeNifF32(serializedTransform, synthetic.shapeTransformOffset + 4, -5.0F);
    writeNifF32(serializedTransform, synthetic.shapeTransformOffset + 8, 6.0F);
    for (std::size_t index = 0; index < kSerializedRotation.size(); ++index)
    {
        writeNifF32(serializedTransform,
                    synthetic.shapeTransformOffset + 12 + index * sizeof(float),
                    kSerializedRotation[index]);
    }
    writeNifF32(serializedTransform, synthetic.shapeTransformOffset + 48, 2.5F);
    const auto transformedDocument = ParseNifDocument(serializedTransform);
    expect(transformedDocument &&
               transformedDocument->triShapes[0].object.translation ==
                   std::array{ 4.0F, -5.0F, 6.0F } &&
               transformedDocument->triShapes[0].object.rotation ==
                   kSerializedRotation &&
               transformedDocument->triShapes[0].object.scale == 2.5F,
           "serialized shape translation, rotation, and scale retain exact f32 values");

    bool allTruncationsRejected = true;
    for (std::size_t size = 0; size < synthetic.bytes.size(); ++size)
    {
        allTruncationsRejected = allTruncationsRejected &&
                                 !ParseNifDocument(std::span(synthetic.bytes).first(size));
    }
    expect(allTruncationsRejected,
           "every truncated synthetic model fixture prefix is rejected");
    for (const auto [offset, label] :
         std::array{
             std::pair{ synthetic.rootReferenceOffset, "root" },
             std::pair{ synthetic.childReferenceOffset, "child" },
             std::pair{ synthetic.dataReferenceOffset, "data" },
             std::pair{ synthetic.propertyReferenceOffset, "property" },
         })
    {
        auto corrupted = synthetic.bytes;
        writeU32Be(corrupted, offset, 4);
        const auto result = ParseNifDocument(corrupted);
        expect(!result && result.error() == NifDocumentError::InvalidLayout,
               std::string("out-of-range synthetic ") + label + " reference is rejected");
    }
}

void testNifMaterialPropertyInventory()
{
    using namespace rerevved::tools;

    const auto synthetic = makeSyntheticMaterialNif();
    const auto parsed    = ParseNifDocument(synthetic.bytes);
    expect(parsed && parsed->materialProperties.size() == 2,
           "multiple synthetic material properties parse and retain in block order");
    if (parsed && parsed->materialProperties.size() == 2)
    {
        const auto retainedBits = [](const NifMaterialPropertyInventory& material)
        {
            return std::array{
                std::bit_cast<std::uint32_t>(material.ambientColor[0]),
                std::bit_cast<std::uint32_t>(material.ambientColor[1]),
                std::bit_cast<std::uint32_t>(material.ambientColor[2]),
                std::bit_cast<std::uint32_t>(material.diffuseColor[0]),
                std::bit_cast<std::uint32_t>(material.diffuseColor[1]),
                std::bit_cast<std::uint32_t>(material.diffuseColor[2]),
                std::bit_cast<std::uint32_t>(material.specularColor[0]),
                std::bit_cast<std::uint32_t>(material.specularColor[1]),
                std::bit_cast<std::uint32_t>(material.specularColor[2]),
                std::bit_cast<std::uint32_t>(material.emissiveColor[0]),
                std::bit_cast<std::uint32_t>(material.emissiveColor[1]),
                std::bit_cast<std::uint32_t>(material.emissiveColor[2]),
                std::bit_cast<std::uint32_t>(material.glossiness),
                std::bit_cast<std::uint32_t>(material.alpha),
            };
        };

        expect(parsed->materialProperties[0].blockIndex == 0 &&
                   parsed->materialProperties[1].blockIndex == 1,
               "material inventory identity follows exact source block order");
        expect(retainedBits(parsed->materialProperties[0]) == kSyntheticMaterialBitsA,
               "ambient, diffuse, specular, emissive, glossiness, and alpha bits retain exact order");
        expect(retainedBits(parsed->materialProperties[1]) == kSyntheticMaterialBitsB,
               "a second material retains independent ordered f32 values");
        expect(std::signbit(parsed->materialProperties[0].specularColor[1]) &&
                   std::isnan(parsed->materialProperties[0].specularColor[2]) &&
                   std::isinf(parsed->materialProperties[0].emissiveColor[0]) &&
                   std::isinf(parsed->materialProperties[0].emissiveColor[1]),
               "signed zero and representative non-finite values remain accepted and unchanged");
    }

    constexpr std::size_t kObjectNetSize     = 12;
    constexpr std::size_t kMaterialSize      = 68;
    const auto            parseWithFirstSize = [&](std::size_t retainedSize)
    {
        auto bytes = synthetic.bytes;
        writeU32Be(bytes,
                   synthetic.blockSizeOffsets[0],
                   static_cast<std::uint32_t>(retainedSize));
        bytes.erase(bytes.begin() + static_cast<std::ptrdiff_t>(
                                        synthetic.payloadOffsets[0] + retainedSize),
                    bytes.begin() + static_cast<std::ptrdiff_t>(
                                        synthetic.payloadOffsets[0] + kMaterialSize));
        return ParseNifDocument(bytes);
    };

    bool inheritedTruncationsRejected = true;
    for (const auto retainedSize : std::array<std::size_t, 6>{ 0, 3, 4, 7, 8, 11 })
    {
        inheritedTruncationsRejected =
            inheritedTruncationsRejected && !parseWithFirstSize(retainedSize);
    }
    expect(inheritedTruncationsRejected,
           "truncated inherited NiObjectNET fields are rejected within the material block");

    bool derivedBoundariesRejected = true;
    for (const auto derivedSize : std::array<std::size_t, 7>{ 0, 12, 24, 36, 48, 52, 55 })
    {
        derivedBoundariesRejected =
            derivedBoundariesRejected &&
            !parseWithFirstSize(kObjectNetSize + derivedSize);
    }
    expect(derivedBoundariesRejected,
           "truncation at every material field boundary and within alpha is rejected");

    auto invalidName = synthetic.bytes;
    writeU32Be(invalidName, synthetic.payloadOffsets[0], 2);
    const auto invalidNameResult = ParseNifDocument(invalidName);
    expect(!invalidNameResult &&
               invalidNameResult.error() == NifDocumentError::InvalidLayout,
           "out-of-range inherited material name index is rejected");

    auto invalidController = synthetic.bytes;
    writeU32Be(invalidController, synthetic.payloadOffsets[0] + 8, 2);
    const auto invalidControllerResult = ParseNifDocument(invalidController);
    expect(!invalidControllerResult &&
               invalidControllerResult.error() == NifDocumentError::InvalidLayout,
           "out-of-range inherited material controller reference is rejected");

    auto oversizedExtraData = synthetic.bytes;
    writeU32Be(oversizedExtraData, synthetic.payloadOffsets[0] + 4, 0xFFFFFFFF);
    const auto oversizedExtraDataResult = ParseNifDocument(oversizedExtraData);
    expect(!oversizedExtraDataResult &&
               oversizedExtraDataResult.error() == NifDocumentError::InvalidLayout,
           "oversized inherited material extra-data count is rejected before allocation");

    auto trailingPayload = synthetic.bytes;
    trailingPayload.insert(
        trailingPayload.begin() +
            static_cast<std::ptrdiff_t>(synthetic.payloadOffsets[0] + kMaterialSize),
        std::byte{ 0xA5 });
    writeU32Be(trailingPayload,
               synthetic.blockSizeOffsets[0],
               static_cast<std::uint32_t>(kMaterialSize + 1));
    const auto trailingPayloadResult = ParseNifDocument(trailingPayload);
    expect(!trailingPayloadResult &&
               trailingPayloadResult.error() == NifDocumentError::InvalidLayout,
           "extra NiMaterialProperty payload bytes are rejected by exact consumption");
}

void testNifTextureSourceInventory()
{
    using namespace rerevved::tools;

    const auto synthetic = makeSyntheticTextureNif();
    expect(synthetic.bytes == makeSyntheticTextureNif().bytes,
           "synthetic texture-source fixture generation is deterministic");
    const auto parsed = ParseNifDocument(synthetic.bytes);
    expect(parsed && parsed->texturingProperties.size() == 2 &&
               parsed->sourceTextures.size() == 2,
           "multiple texturing properties and source textures retain source order");
    if (parsed && parsed->texturingProperties.size() == 2 &&
        parsed->sourceTextures.size() == 2)
    {
        const auto&                 first  = parsed->texturingProperties[0];
        const auto&                 second = parsed->texturingProperties[1];
        std::array<std::uint8_t, 9> firstPresence{};
        std::array<std::uint8_t, 9> secondPresence{};
        for (std::size_t index = 0; index < first.standardSlots.size(); ++index)
        {
            firstPresence[index]  = first.standardSlots[index].presence;
            secondPresence[index] = second.standardSlots[index].presence;
        }
        expect(first.blockIndex == 0 && second.blockIndex == 1 &&
                   first.flags == 0xA5F3 && second.flags == 0x55AA &&
                   first.textureCount == 9 && second.textureCount == 9,
               "texturing property identity, raw flags, and count retain exact values");
        expect(firstPresence == std::array<std::uint8_t, 9>{ 1, 2, 0, 0x7F, 1, 1, 0, 1, 0 } &&
                   secondPresence ==
                       std::array<std::uint8_t, 9>{ 0, 2, 3, 4, 5, 6, 7, 8, 9 },
               "all nine standard slot presence bytes retain fixed source order");
        expect(!first.standardSlots[1].descriptor &&
                   !first.standardSlots[3].descriptor &&
                   std::ranges::none_of(second.standardSlots,
                                        [](const auto& slot)
                                        {
                                            return slot.descriptor.has_value();
                                        }),
               "standard slot bytes other than exact one consume no descriptor body");

        const auto& base = first.standardSlots[0];
        expect(base.descriptor && base.descriptor->source == 2 &&
                   base.descriptor->flags == 0xABCD &&
                   base.descriptor->hasTextureTransform == 1 &&
                   base.descriptor->transform,
               "Base slot retains its source, raw flags, and exact transform presence");
        if (base.descriptor && base.descriptor->transform)
        {
            const auto&      transform = *base.descriptor->transform;
            const std::array retainedBits{
                std::bit_cast<std::uint32_t>(transform.translation[0]),
                std::bit_cast<std::uint32_t>(transform.translation[1]),
                std::bit_cast<std::uint32_t>(transform.scale[0]),
                std::bit_cast<std::uint32_t>(transform.scale[1]),
                std::bit_cast<std::uint32_t>(transform.rotation),
                transform.transformMethod,
                std::bit_cast<std::uint32_t>(transform.center[0]),
                std::bit_cast<std::uint32_t>(transform.center[1]),
            };
            expect(retainedBits ==
                       std::array<std::uint32_t, 8>{ 0x00000000,
                                                     0x80000000,
                                                     0x7F800000,
                                                     0xFF800000,
                                                     0x7FC12345,
                                                     0xA5A5A5A5,
                                                     0x3F000000,
                                                     0xBF000000 },
                   "texture transform fields retain exact component order and f32 bits");
        }

        const auto& glow = first.standardSlots[4];
        expect(glow.descriptor && glow.descriptor->source == 3 &&
                   glow.descriptor->hasTextureTransform == 2 &&
                   !glow.descriptor->transform,
               "transform bytes other than exact one retain no transform body");

        const auto& bump = first.standardSlots[5];
        expect(bump.descriptor && bump.bump && !bump.parallaxOffset,
               "present Bump retains its descriptor and unconditional extension only");
        if (bump.bump)
        {
            const std::array retainedBits{
                std::bit_cast<std::uint32_t>(bump.bump->lumaScale),
                std::bit_cast<std::uint32_t>(bump.bump->lumaOffset),
                std::bit_cast<std::uint32_t>(bump.bump->bumpMatrix[0]),
                std::bit_cast<std::uint32_t>(bump.bump->bumpMatrix[1]),
                std::bit_cast<std::uint32_t>(bump.bump->bumpMatrix[2]),
                std::bit_cast<std::uint32_t>(bump.bump->bumpMatrix[3]),
            };
            expect(retainedBits ==
                       std::array<std::uint32_t, 6>{ 0x80000000,
                                                     0x7F800000,
                                                     0xFF800000,
                                                     0x7FC54321,
                                                     0x00000001,
                                                     0xBF800000 },
                   "Bump luma and matrix values retain exact order and f32 bits");
        }

        const auto& parallax = first.standardSlots[7];
        expect(parallax.descriptor && parallax.descriptor->source == UINT32_MAX &&
                   parallax.parallaxOffset &&
                   std::bit_cast<std::uint32_t>(*parallax.parallaxOffset) == 0x7FC0BEEF,
               "Parallax retains a null source and exact unconditional offset bits");

        expect(first.shaderTextureCount == 3 && first.shaderTextures.size() == 3 &&
                   second.shaderTextureCount == 0 && second.shaderTextures.empty(),
               "shader counts control exact ordered retained record sequences");
        if (first.shaderTextures.size() == 3)
        {
            expect(first.shaderTextures[0].hasMap == 1 &&
                       first.shaderTextures[0].descriptor &&
                       first.shaderTextures[0].mapId == 0xDEADBEEF &&
                       first.shaderTextures[1].hasMap == 2 &&
                       !first.shaderTextures[1].descriptor &&
                       !first.shaderTextures[1].mapId &&
                       first.shaderTextures[2].hasMap == 1 &&
                       first.shaderTextures[2].descriptor &&
                       first.shaderTextures[2].mapId == 0xCAFEBABE,
                   "shader presence, optional descriptors, and raw Map IDs retain source order");
            const auto& shaderTransform = first.shaderTextures[2].descriptor->transform;
            if (shaderTransform)
            {
                const std::array retainedBits{
                    std::bit_cast<std::uint32_t>(shaderTransform->translation[0]),
                    std::bit_cast<std::uint32_t>(shaderTransform->translation[1]),
                    std::bit_cast<std::uint32_t>(shaderTransform->scale[0]),
                    std::bit_cast<std::uint32_t>(shaderTransform->scale[1]),
                    std::bit_cast<std::uint32_t>(shaderTransform->rotation),
                    shaderTransform->transformMethod,
                    std::bit_cast<std::uint32_t>(shaderTransform->center[0]),
                    std::bit_cast<std::uint32_t>(shaderTransform->center[1]),
                };
                expect(retainedBits ==
                           std::array<std::uint32_t, 8>{ 0x3F800000,
                                                         0x40000000,
                                                         0x40400000,
                                                         0x40800000,
                                                         0x40A00000,
                                                         0x12345678,
                                                         0x40C00000,
                                                         0x40E00000 },
                       "a shader descriptor retains an independent ordered transform");
            }
        }

        const auto& external = parsed->sourceTextures[0];
        const auto& opaque   = parsed->sourceTextures[1];
        expect(external.blockIndex == 2 && external.useExternal == 1 &&
                   external.fileNameIndex == 0 && external.pixelData == UINT32_MAX &&
                   external.pixelLayout == 0x11111110 &&
                   external.useMipmaps == 0x11111111 &&
                   external.alphaFormat == 0x11111112 && external.isStatic == 2 &&
                   external.directRender == 3 && external.persistRenderData == 4,
               "external source retains its exact 24-byte ordered payload");
        expect(external.IsSupportedExternalSource(),
               "only the exact validated external carrier combination is supported");
        expect(opaque.blockIndex == 3 && opaque.useExternal == 0 &&
                   opaque.fileNameIndex == UINT32_MAX && opaque.pixelData == 4 &&
                   opaque.pixelLayout == 0x22222220 &&
                   opaque.useMipmaps == 0x22222221 &&
                   opaque.alphaFormat == 0x22222222 && !opaque.IsSupportedExternalSource(),
               "a structurally valid opaque pixel-data carrier remains unsupported");
    }

    const auto parseTruncatedBlock = [&](std::size_t blockIndex, std::size_t retainedSize)
    {
        auto bytes = synthetic.bytes;
        writeU32Be(bytes,
                   synthetic.blockSizeOffsets[blockIndex],
                   static_cast<std::uint32_t>(retainedSize));
        bytes.erase(bytes.begin() + static_cast<std::ptrdiff_t>(
                                        synthetic.payloadOffsets[blockIndex] + retainedSize),
                    bytes.begin() + static_cast<std::ptrdiff_t>(
                                        synthetic.payloadOffsets[blockIndex] +
                                        synthetic.payloadSizes[blockIndex]));
        return ParseNifDocument(bytes);
    };

    bool propertyTruncationsRejected = true;
    for (std::size_t retainedSize = 0; retainedSize < synthetic.payloadSizes[0];
         ++retainedSize)
    {
        propertyTruncationsRejected =
            propertyTruncationsRejected && !parseTruncatedBlock(0, retainedSize);
    }
    expect(propertyTruncationsRejected,
           "every truncated inherited, fixed, and conditional texturing-property prefix is rejected");

    bool sourceTruncationsRejected = true;
    for (std::size_t sourceIndex = 0; sourceIndex < 2; ++sourceIndex)
    {
        for (std::size_t retainedSize = 0;
             retainedSize < synthetic.payloadSizes[sourceIndex + 2];
             ++retainedSize)
        {
            sourceTruncationsRejected =
                sourceTruncationsRejected &&
                !parseTruncatedBlock(sourceIndex + 2, retainedSize);
        }
    }
    expect(sourceTruncationsRejected,
           "every truncated inherited and fixed NiSourceTexture prefix is rejected");

    auto wrongTextureCount = synthetic.bytes;
    writeU32Be(wrongTextureCount, synthetic.textureCountOffset, 8);
    expect(!ParseNifDocument(wrongTextureCount),
           "a texturing-property Texture Count other than nine is rejected");

    auto oversizedShaderCount = synthetic.bytes;
    writeU32Be(oversizedShaderCount, synthetic.shaderCountOffset, UINT32_MAX);
    expect(!ParseNifDocument(oversizedShaderCount),
           "an oversized shader count is rejected before allocation");

    bool allInvalidDescriptorSourcesRejected = true;
    bool allNullDescriptorSourcesRetained    = true;
    for (const auto sourceOffset : synthetic.descriptorSourceOffsets)
    {
        auto invalidDescriptorSource = synthetic.bytes;
        writeU32Be(invalidDescriptorSource, sourceOffset, 5);
        allInvalidDescriptorSourcesRejected =
            allInvalidDescriptorSourcesRejected &&
            !ParseNifDocument(invalidDescriptorSource);

        auto nullDescriptorSource = synthetic.bytes;
        writeU32Be(nullDescriptorSource, sourceOffset, UINT32_MAX);
        allNullDescriptorSourcesRetained =
            allNullDescriptorSourcesRetained &&
            ParseNifDocument(nullDescriptorSource).has_value();
    }
    expect(allInvalidDescriptorSourcesRejected,
           "out-of-range standard and shader descriptor source references are rejected");
    expect(allNullDescriptorSourcesRetained,
           "null standard and shader descriptor source references are retained");

    auto invalidSourceName = synthetic.bytes;
    writeU32Be(invalidSourceName, synthetic.sourceNameOffsets[0], 1);
    expect(!ParseNifDocument(invalidSourceName),
           "an out-of-range source filename index is rejected");
    auto nullSourceName = synthetic.bytes;
    writeU32Be(nullSourceName, synthetic.sourceNameOffsets[0], UINT32_MAX);
    const auto nullNameResult = ParseNifDocument(nullSourceName);
    expect(nullNameResult && !nullNameResult->sourceTextures[0].IsSupportedExternalSource(),
           "a null source filename remains structurally valid but unsupported");

    auto invalidPixelReference = synthetic.bytes;
    writeU32Be(invalidPixelReference, synthetic.sourcePixelOffsets[1], 5);
    expect(!ParseNifDocument(invalidPixelReference),
           "an out-of-range source pixel-data reference is rejected");
    auto nullPixelReference = synthetic.bytes;
    writeU32Be(nullPixelReference, synthetic.sourcePixelOffsets[1], UINT32_MAX);
    expect(ParseNifDocument(nullPixelReference).has_value(),
           "a null source pixel-data reference is retained");

    auto externalWithPixel = synthetic.bytes;
    writeU32Be(externalWithPixel, synthetic.sourcePixelOffsets[0], 4);
    const auto externalWithPixelResult = ParseNifDocument(externalWithPixel);
    expect(externalWithPixelResult &&
               !externalWithPixelResult->sourceTextures[0].IsSupportedExternalSource(),
           "Use External one with a non-null Pixel Data reference remains unsupported");

    auto unsupportedExternalValue                               = synthetic.bytes;
    unsupportedExternalValue[synthetic.sourceDerivedOffsets[0]] = std::byte{ 2 };
    const auto unsupportedExternalResult                        = ParseNifDocument(unsupportedExternalValue);
    expect(unsupportedExternalResult &&
               unsupportedExternalResult->sourceTextures[0].useExternal == 2 &&
               !unsupportedExternalResult->sourceTextures[0].IsSupportedExternalSource(),
           "a raw Use External byte other than exact one remains unsupported");

    auto invalidPropertyName = synthetic.bytes;
    writeU32Be(invalidPropertyName, synthetic.firstPropertyNameOffset, 1);
    expect(!ParseNifDocument(invalidPropertyName),
           "an invalid inherited texturing-property name index is rejected");
    auto invalidPropertyController = synthetic.bytes;
    writeU32Be(invalidPropertyController, synthetic.firstPropertyControllerOffset, 5);
    expect(!ParseNifDocument(invalidPropertyController),
           "an invalid inherited texturing-property controller reference is rejected");
    auto oversizedPropertyExtraData = synthetic.bytes;
    writeU32Be(oversizedPropertyExtraData,
               synthetic.firstPropertyNameOffset + 4,
               UINT32_MAX);
    expect(!ParseNifDocument(oversizedPropertyExtraData),
           "an oversized inherited texturing-property extra-data count is rejected");

    auto invalidSourceController = synthetic.bytes;
    writeU32Be(invalidSourceController, synthetic.payloadOffsets[2] + 8, 5);
    expect(!ParseNifDocument(invalidSourceController),
           "an invalid inherited source-texture controller reference is rejected");
    auto oversizedSourceExtraData = synthetic.bytes;
    writeU32Be(oversizedSourceExtraData, synthetic.payloadOffsets[2] + 4, UINT32_MAX);
    expect(!ParseNifDocument(oversizedSourceExtraData),
           "an oversized inherited source-texture extra-data count is rejected");

    auto trailingProperty = synthetic.bytes;
    trailingProperty.insert(
        trailingProperty.begin() + static_cast<std::ptrdiff_t>(
                                       synthetic.payloadOffsets[0] +
                                       synthetic.payloadSizes[0]),
        std::byte{ 0xA5 });
    writeU32Be(trailingProperty,
               synthetic.blockSizeOffsets[0],
               static_cast<std::uint32_t>(synthetic.payloadSizes[0] + 1));
    expect(!ParseNifDocument(trailingProperty),
           "extra texturing-property payload bytes are rejected by exact consumption");

    auto trailingSource = synthetic.bytes;
    trailingSource.insert(
        trailingSource.begin() + static_cast<std::ptrdiff_t>(
                                     synthetic.payloadOffsets[2] +
                                     synthetic.payloadSizes[2]),
        std::byte{ 0x5A });
    writeU32Be(trailingSource,
               synthetic.blockSizeOffsets[2],
               static_cast<std::uint32_t>(synthetic.payloadSizes[2] + 1));
    expect(!ParseNifDocument(trailingSource),
           "extra source-texture payload bytes are rejected by exact consumption");
}

void testNifMaterialInspectorPresentation()
{
    using namespace rerevved::tools;

    const auto parsed = ParseNifDocument(makeSyntheticMaterialNif().bytes);
    expect(parsed.has_value(), "synthetic materials parse for Inspector presentation");
    if (!parsed)
    {
        return;
    }

    const auto text = FormatNifMaterialProperties(parsed->materialProperties);
    expect(text.size() == 2 && text[0].heading == "NiMaterialProperty block 0" &&
               text[1].heading == "NiMaterialProperty block 1",
           "material Inspector presents every property in source block order");
    if (text.size() == 2)
    {
        expect(text[0].fields ==
                   std::array<std::string, 6>{
                       "Ambient color: 1.25, -2.5, 3.75",
                       "Diffuse color: -4.5, 5.25, -6.75",
                       "Specular color: 0, -0, NaN",
                       "Emissive color: +infinity, -infinity, 1.40129846e-45",
                       "Glossiness: 7.5",
                       "Alpha: -8.25",
                   },
               "material Inspector preserves exact field, component, and special-value order");
        expect(text[1].fields[0] == "Ambient color: 10, 11, 12" &&
                   text[1].fields[5] == "Alpha: 23",
               "a second material remains clearly independent and ordered");
    }

    for (const auto bits : std::array<std::uint32_t, 4>{
             0x3EAAAAAB, 0x3F800001, 0x00800001, 0x7F7FFFFF })
    {
        const auto value     = std::bit_cast<float>(bits);
        const auto valueText = FormatNifInspectorFloat(value);
        float      roundTrip{};
        const auto result = std::from_chars(valueText.data(),
                                            valueText.data() + valueText.size(),
                                            roundTrip,
                                            std::chars_format::general);
        expect(result.ec == std::errc{} && result.ptr == valueText.data() + valueText.size() &&
                   std::bit_cast<std::uint32_t>(roundTrip) == bits,
               "finite material text round-trips to the exact retained f32 bits");
    }
    expect(FormatNifInspectorFloat(0.0F) == "0" &&
               FormatNifInspectorFloat(-0.0F) == "-0" &&
               FormatNifInspectorFloat(std::numeric_limits<float>::infinity()) == "+infinity" &&
               FormatNifInspectorFloat(-std::numeric_limits<float>::infinity()) == "-infinity" &&
               FormatNifInspectorFloat(std::numeric_limits<float>::quiet_NaN()) == "NaN",
           "material Inspector distinguishes signed zero and names infinities and NaN explicitly");

    const NifDocument empty;
    expect(FormatNifMaterialProperties(empty.materialProperties).empty(),
           "empty material inventory produces no property presentation sections");

    FpkEntryDocument embedded{ 3, FpkEntryFormat::Nif, *parsed };
    const auto*      embeddedNif         = std::get_if<NifDocument>(&embedded.data);
    const auto       embeddedText        = embeddedNif
                                               ? FormatNifMaterialProperties(embeddedNif->materialProperties)
                                               : std::vector<NifMaterialPropertyText>{};
    bool             presentationMatches = embeddedText.size() == text.size();
    for (std::size_t index = 0; presentationMatches && index < text.size(); ++index)
    {
        presentationMatches = embeddedText[index].heading == text[index].heading &&
                              embeddedText[index].fields == text[index].fields;
    }
    expect(embeddedNif && presentationMatches,
           "direct and explicitly opened archive NIFs share identical material presentation");
}

void testNifTextureSourceInspectorPresentation()
{
    using namespace rerevved::tools;

    const auto parsed = ParseNifDocument(makeSyntheticTextureNif().bytes);
    expect(parsed.has_value(), "synthetic texture sources parse for Inspector presentation");
    if (!parsed)
    {
        return;
    }

    const auto properties = FormatNifTexturingProperties(parsed->texturingProperties);
    expect(properties.size() == 2 &&
               properties[0].heading == "NiTexturingProperty block 0" &&
               properties[1].heading == "NiTexturingProperty block 1",
           "texturing properties retain source block order in Inspector presentation");
    if (properties.size() == 2)
    {
        const std::vector<std::string> expectedFirst{
            "Flags: 0xA5F3",
            "Texture Count: 9",
            "Base presence: 1 (0x01)",
            "Base descriptor: present",
            "Base Source block reference: 2 (0x00000002)",
            "Base descriptor Flags: 0xABCD",
            "Base Has Texture Transform: 1 (0x01)",
            "Base Texture Transform: present",
            "Base Translation X/Y: 0, -0",
            "Base Scale X/Y: +infinity, -infinity",
            "Base Rotation: NaN",
            "Base Transform Method: 2779096485 (0xA5A5A5A5)",
            "Base Center X/Y: 0.5, -0.5",
            "Dark presence: 2 (0x02)",
            "Dark descriptor: absent",
            "Detail presence: 0 (0x00)",
            "Detail descriptor: absent",
            "Gloss presence: 127 (0x7F)",
            "Gloss descriptor: absent",
            "Glow presence: 1 (0x01)",
            "Glow descriptor: present",
            "Glow Source block reference: 3 (0x00000003)",
            "Glow descriptor Flags: 0x1020",
            "Glow Has Texture Transform: 2 (0x02)",
            "Glow Texture Transform: absent",
            "Bump presence: 1 (0x01)",
            "Bump descriptor: present",
            "Bump Source block reference: 2 (0x00000002)",
            "Bump descriptor Flags: 0x3040",
            "Bump Has Texture Transform: 0 (0x00)",
            "Bump Texture Transform: absent",
            "Bump Luma Scale: -0",
            "Bump Luma Offset: +infinity",
            "Bump Matrix: -infinity, NaN, 1.40129846e-45, -1",
            "Normal presence: 0 (0x00)",
            "Normal descriptor: absent",
            "Parallax presence: 1 (0x01)",
            "Parallax descriptor: present",
            "Parallax Source block reference: none (0xFFFFFFFF)",
            "Parallax descriptor Flags: 0x5060",
            "Parallax Has Texture Transform: 0 (0x00)",
            "Parallax Texture Transform: absent",
            "Parallax Offset: NaN",
            "Decal 0 presence: 0 (0x00)",
            "Decal 0 descriptor: absent",
            "Shader Texture Count: 3",
            "Shader 1 Has Map: 1 (0x01)",
            "Shader 1 descriptor: present",
            "Shader 1 Source block reference: 2 (0x00000002)",
            "Shader 1 descriptor Flags: 0x7080",
            "Shader 1 Has Texture Transform: 0 (0x00)",
            "Shader 1 Texture Transform: absent",
            "Shader 1 Map ID: 3735928559 (0xDEADBEEF)",
            "Shader 2 Has Map: 2 (0x02)",
            "Shader 2 descriptor: absent",
            "Shader 3 Has Map: 1 (0x01)",
            "Shader 3 descriptor: present",
            "Shader 3 Source block reference: 3 (0x00000003)",
            "Shader 3 descriptor Flags: 0x90A0",
            "Shader 3 Has Texture Transform: 1 (0x01)",
            "Shader 3 Texture Transform: present",
            "Shader 3 Translation X/Y: 1, 2",
            "Shader 3 Scale X/Y: 3, 4",
            "Shader 3 Rotation: 5",
            "Shader 3 Transform Method: 305419896 (0x12345678)",
            "Shader 3 Center X/Y: 6, 7",
            "Shader 3 Map ID: 3405691582 (0xCAFEBABE)",
        };
        expect(properties[0].fields == expectedFirst,
               "texturing Inspector preserves exact slot, descriptor, extension, and shader order");
        expect(properties[1].fields.size() == 21 &&
                   properties[1].fields[0] == "Flags: 0x55AA" &&
                   properties[1].fields[2] == "Base presence: 0 (0x00)" &&
                   properties[1].fields[4] == "Dark presence: 2 (0x02)" &&
                   properties[1].fields[18] == "Decal 0 presence: 9 (0x09)" &&
                   properties[1].fields[20] == "Shader Texture Count: 0",
               "absent descriptors retain raw presence bytes without presentation bodies");
    }

    const auto hasField = [](const std::vector<std::string>& fields, std::string_view value)
    {
        return std::ranges::find(fields, value) != fields.end();
    };
    const auto hasFieldPrefix = [](const std::vector<std::string>& fields,
                                   std::string_view                prefix)
    {
        return std::ranges::any_of(fields,
                                   [&](const auto& field)
                                   {
                                       return field.starts_with(prefix);
                                   });
    };

    auto nonSelectedSlot                                             = *parsed;
    nonSelectedSlot.texturingProperties[0].standardSlots[0].presence = 2;
    const auto nonSelectedSlotText =
        FormatNifTexturingProperties(nonSelectedSlot.texturingProperties);
    expect(hasField(nonSelectedSlotText[0].fields, "Base descriptor: absent") &&
               !hasFieldPrefix(nonSelectedSlotText[0].fields,
                               "Base Source block reference"),
           "a descriptor body is presented only when its raw slot presence equals one");

    auto nonSelectedTransform = *parsed;
    nonSelectedTransform.texturingProperties[0]
        .standardSlots[0]
        .descriptor->hasTextureTransform = 2;
    const auto nonSelectedTransformText =
        FormatNifTexturingProperties(nonSelectedTransform.texturingProperties);
    expect(hasField(nonSelectedTransformText[0].fields,
                    "Base Texture Transform: absent") &&
               !hasFieldPrefix(nonSelectedTransformText[0].fields,
                               "Base Translation X/Y"),
           "transform values are presented only when the raw transform byte equals one");

    auto nonSelectedShader                                            = *parsed;
    nonSelectedShader.texturingProperties[0].shaderTextures[0].hasMap = 2;
    const auto nonSelectedShaderText =
        FormatNifTexturingProperties(nonSelectedShader.texturingProperties);
    expect(hasField(nonSelectedShaderText[0].fields, "Shader 1 descriptor: absent") &&
               !hasFieldPrefix(nonSelectedShaderText[0].fields,
                               "Shader 1 Source block reference") &&
               !hasFieldPrefix(nonSelectedShaderText[0].fields, "Shader 1 Map ID"),
           "shader descriptor and Map ID presentation require raw Has Map equal to one");

    auto unresolvedSource                                                       = *parsed;
    unresolvedSource.texturingProperties[0].standardSlots[0].descriptor->source = 77;
    unresolvedSource.sourceTextures.clear();
    const auto unresolvedSourceText =
        FormatNifTexturingProperties(unresolvedSource.texturingProperties);
    expect(hasField(unresolvedSourceText[0].fields,
                    "Base Source block reference: 77 (0x0000004D)"),
           "descriptor source references remain visible without a retained source inventory");

    const auto sources = FormatNifSourceTextures(*parsed);
    expect(sources.size() == 2 && sources[0].heading == "NiSourceTexture block 2" &&
               sources[1].heading == "NiSourceTexture block 3",
           "source textures retain source block order in Inspector presentation");
    if (sources.size() == 2)
    {
        expect(sources[0].fields ==
                   std::vector<std::string>{
                       "Use External: 1 (0x01)",
                       "File Name string index: 0 (0x00000000)",
                       "File Name bytes: SyntheticTexture.dds",
                       "Pixel Data block reference: none (0xFFFFFFFF)",
                       "Pixel Layout: 286331152 (0x11111110)",
                       "Use Mipmaps: 286331153 (0x11111111)",
                       "Alpha Format: 286331154 (0x11111112)",
                       "Is Static: 2 (0x02)",
                       "Direct Render: 3 (0x03)",
                       "Persist Render Data: 4 (0x04)",
                       "Supported external source",
                   },
               "supported external source presentation retains exact raw fields and metadata bytes");
        expect(sources[1].fields ==
                   std::vector<std::string>{
                       "Use External: 0 (0x00)",
                       "File Name string index: none (0xFFFFFFFF)",
                       "Pixel Data block reference: 4 (0x00000004)",
                       "Pixel Layout: 572662304 (0x22222220)",
                       "Use Mipmaps: 572662305 (0x22222221)",
                       "Alpha Format: 572662306 (0x22222222)",
                       "Is Static: 2 (0x02)",
                       "Direct Render: 3 (0x03)",
                       "Persist Render Data: 4 (0x04)",
                       "Unsupported source combination",
                   },
               "unsupported source presentation keeps its opaque block reference visible");
    }

    const std::array printable{ std::byte{ 'A' }, std::byte{ 'z' }, std::byte{ ' ' } };
    const std::array withNul{ std::byte{ 'A' }, std::byte{ 0 }, std::byte{ 'B' } };
    const std::array nonPrintable{ std::byte{ 0xFF }, std::byte{ '\\' }, std::byte{ 0x1F } };
    expect(FormatNifStringBytes(printable) == "Az " && FormatNifStringBytes({}).empty() &&
               FormatNifStringBytes(withNul) == "A\\x00B" &&
               FormatNifStringBytes(nonPrintable) == "\\xFF\\\\\\x1F",
           "retained string bytes use safe printable, empty, NUL, slash, and hex escaping");

    NifDocument escapedDocument;
    escapedDocument.strings = {
        {},
        { std::byte{ 'A' }, std::byte{ 0 }, std::byte{ 'B' } },
        { std::byte{ 0xFF }, std::byte{ '\\' } },
    };
    for (std::uint32_t index = 0; index < escapedDocument.strings.size(); ++index)
    {
        NifSourceTextureInventory source{};
        source.blockIndex    = index;
        source.useExternal   = 1;
        source.fileNameIndex = index;
        escapedDocument.sourceTextures.push_back(source);
    }
    const auto escapedSources = FormatNifSourceTextures(escapedDocument);
    expect(escapedSources.size() == 3 &&
               escapedSources[0].fields[2] == "File Name bytes: <empty>" &&
               escapedSources[1].fields[2] == "File Name bytes: A\\x00B" &&
               escapedSources[2].fields[2] == "File Name bytes: \\xFF\\\\" &&
               escapedSources[0].fields.back() == "Supported external source",
           "source metadata displays empty, NUL-containing, and non-printable bytes without path interpretation");

    for (const auto mutation : std::array{ 0, 1, 2 })
    {
        auto unsupported = *parsed;
        if (mutation == 0)
        {
            unsupported.sourceTextures[0].useExternal = 0;
        }
        else if (mutation == 1)
        {
            unsupported.sourceTextures[0].fileNameIndex = UINT32_MAX;
        }
        else
        {
            unsupported.sourceTextures[0].pixelData = 4;
        }
        const auto unsupportedText = FormatNifSourceTextures(unsupported);
        expect(!unsupportedText.empty() &&
                   unsupportedText[0].fields.back() == "Unsupported source combination",
               "each unsupported source-carrier combination receives the neutral classification");
    }

    auto invalidName                            = *parsed;
    invalidName.sourceTextures[0].fileNameIndex = 99;
    const auto invalidNameText                  = FormatNifSourceTextures(invalidName);
    expect(!hasFieldPrefix(invalidNameText[0].fields, "File Name bytes:") &&
               invalidNameText[0].fields.back() == "Unsupported source combination",
           "an out-of-range retained filename index is not dereferenced or classified external");

    const NifDocument empty;
    expect(FormatNifTexturingProperties(empty.texturingProperties).empty() &&
               FormatNifSourceTextures(empty).empty(),
           "empty texturing and source inventories produce neutral empty presentation inputs");

    FpkEntryDocument embedded{ 7, FpkEntryFormat::Nif, *parsed };
    const auto*      embeddedNif        = std::get_if<NifDocument>(&embedded.data);
    const auto       embeddedProperties = embeddedNif
                                              ? FormatNifTexturingProperties(
                                                    embeddedNif->texturingProperties)
                                              : std::vector<NifTexturingPropertyText>{};
    const auto       embeddedSources    = embeddedNif ? FormatNifSourceTextures(*embeddedNif)
                                                      : std::vector<NifSourceTextureText>{};
    const auto       sameText           = [](const auto& left, const auto& right)
    {
        if (left.size() != right.size())
        {
            return false;
        }
        for (std::size_t index = 0; index < left.size(); ++index)
        {
            if (left[index].heading != right[index].heading ||
                left[index].fields != right[index].fields)
            {
                return false;
            }
        }
        return true;
    };
    expect(embeddedNif && sameText(properties, embeddedProperties) &&
               sameText(sources, embeddedSources),
           "direct and explicitly opened archive NIFs share identical texture-source presentation");
}

void testNifSourceNavigationFixture()
{
    using namespace rerevved::tools;

    const auto parsed = ParseNifDocument(makeSyntheticSourceNavigationNif());
    expect(parsed && parsed->sourceTextures.size() == 3 && parsed->strings.size() == 3,
           "multi-record source-texture navigation fixture parses with three retained records");
    if (!parsed)
    {
        return;
    }

    std::vector<std::string> labels;
    labels.reserve(parsed->sourceTextures.size());
    for (std::size_t index = 0; index < parsed->sourceTextures.size(); ++index)
    {
        const auto& source = parsed->sourceTextures[index];
        labels.push_back("Record " + std::to_string(index) + " / Block " +
                         std::to_string(source.blockIndex) + ": NiSourceTexture / File " +
                         FormatNifStringBytes(parsed->strings[source.fileNameIndex]));
    }
    expect(labels[0].substr(labels[0].find(" / File ")) ==
               labels[1].substr(labels[1].find(" / File ")),
           "source-texture navigation labels preserve duplicate identities");
    expect(labels[0].find("##") != std::string::npos &&
               labels[0].find("\\x00") != std::string::npos,
           "source-texture navigation labels preserve escaped delimiter bytes");
    expect(labels[2].size() > 100,
           "source-texture navigation labels preserve long escaped names");
    expect(FilterInspectorIndices(labels, "DUP##") == std::vector<std::size_t>{ 0, 1 } &&
               FilterInspectorIndices(labels, "\\xFF") == std::vector<std::size_t>{ 2 },
           "source-texture filters match escaped names while retaining source order");

    InspectorNavigationState state;
    expect(SelectInspectorIndex(state, 1, labels.size()) &&
               FilterInspectorIndices(labels, "long").empty() &&
               InspectorSelectionHidden(
                   state,
                   FilterInspectorIndices(labels, "\\xFF")),
           "source-texture selection remains addressable when a long-name filter hides it");
    const FpkEntryDocument embedded{ 6, FpkEntryFormat::Nif, *parsed };
    const auto*            embeddedNif = std::get_if<NifDocument>(&embedded.data);
    const auto             sameText    = [](const auto& left, const auto& right)
    {
        if (left.size() != right.size())
        {
            return false;
        }
        for (std::size_t index = 0; index < left.size(); ++index)
        {
            if (left[index].heading != right[index].heading ||
                left[index].fields != right[index].fields)
            {
                return false;
            }
        }
        return true;
    };
    expect(embeddedNif &&
               sameText(FormatNifSourceTextures(*embeddedNif),
                        FormatNifSourceTextures(*parsed)),
           "source-texture navigation fixture has direct and embedded presentation parity");
}

void testNifModelAssembly()
{
    using namespace rerevved::tools;

    expect(NifModelErrorMessage(NifModelError::SceneCycle) ==
               "The NIF scene graph contains a cycle.",
           "NIF model cycle message");
    expect(NifModelErrorMessage(NifModelError::WrongDataBlockType) ==
               "A NiTriShape data reference does not target NiTriShapeData.",
           "NIF model wrong-data-type message");
    expect(NifModelErrorMessage(NifModelError::InconsistentGeometry) ==
               "The retained NIF geometry arrays are inconsistent.",
           "NIF model inconsistent-geometry message");
    expect(NifModelErrorMessage(NifModelError::TriangleSelectorOutOfRange) ==
               "A NIF triangle selector is outside the retained vertex positions.",
           "NIF model triangle-selector message");
    expect(NifModelErrorMessage(NifModelError::InvalidTransform) ==
               "A NIF scene transform is nonfinite or unrepresentable.",
           "NIF model transform message");
    expect(NifModelErrorMessage(NifModelError::NoSupportedMeshes) ==
               "The NIF scene contains no supported meshes.",
           "NIF model no-mesh message");

    const auto synthetic = makeSyntheticModelNif();
    const auto parsed    = ParseNifDocument(synthetic.bytes);
    expect(parsed.has_value(), "synthetic model parses for model assembly");
    if (!parsed)
    {
        return;
    }

    const auto* positionsBefore = parsed->triShapeData[0].vertexPositions.data();
    const auto* normalsBefore   = parsed->triShapeData[0].normalVectors.data();
    const auto* trianglesBefore = parsed->triShapeData[0].triangles.data();
    const auto  model           = AssembleNifModel(*parsed);
    expect(model && model->meshes.size() == 1,
           "synthetic model assembles exactly one supported mesh");
    if (model && model->meshes.size() == 1)
    {
        const auto& mesh = model->meshes[0];
        expect(mesh.rootIndex == 0 &&
                   mesh.nodePath == std::vector<std::size_t>{ 0 } &&
                   mesh.triShapeIndex == 0 && mesh.triShapeDataIndex == 0 &&
                   mesh.materialPropertyBlocks == std::vector<std::uint32_t>{ 3 },
               "assembled mesh resolves its root, node path, shape, data, and material");
        const auto& geometry = parsed->triShapeData[mesh.triShapeDataIndex];
        expect(geometry.vertexPositions.size() == 3 && geometry.normalVectors.size() == 3 &&
                   geometry.triangles ==
                       std::vector<std::array<std::uint16_t, 3>>{ { 0, 1, 2 } },
               "assembled mesh indices resolve the retained synthetic geometry");
        expect(geometry.vertexPositions.data() == positionsBefore &&
                   geometry.normalVectors.data() == normalsBefore &&
                   geometry.triangles.data() == trianglesBefore,
               "model assembly leaves document-owned geometry storage unchanged");
        const auto transformed = ApplyNifModelTransform(
            mesh.worldTransform, geometry.vertexPositions[1]);
        expect(transformed && *transformed == std::array{ 1.0, 0.0, 0.0 },
               "identity node and shape transforms preserve a retained position");
    }

    constexpr std::array kRotateZPositive{
        0.0F,
        1.0F,
        0.0F,
        -1.0F,
        0.0F,
        0.0F,
        0.0F,
        0.0F,
        1.0F,
    };
    auto shapeLocal                            = *parsed;
    shapeLocal.triShapes[0].object.translation = { 10.0F, 20.0F, 30.0F };
    shapeLocal.triShapes[0].object.rotation    = kRotateZPositive;
    shapeLocal.triShapes[0].object.scale       = 2.0F;
    const auto normalsBeforeTransform          = shapeLocal.triShapeData[0].normalVectors;
    const auto shapeLocalModel                 = AssembleNifModel(shapeLocal);
    const auto shapeLocalPosition              = shapeLocalModel
                                                     ? ApplyNifModelTransform(
                                                           shapeLocalModel->meshes[0].worldTransform,
                                                           { 1.0F, 0.0F, 0.0F })
                                                     : std::expected<std::array<double, 3>, NifModelError>(
                                                           std::unexpected(
                                                               NifModelError::InvalidTransform));
    expect(shapeLocalPosition &&
               *shapeLocalPosition == std::array{ 10.0, 22.0, 30.0 } &&
               shapeLocal.triShapeData[0].normalVectors == normalsBeforeTransform,
           "shape-local translation, rotation, and scale affect positions without changing normals");

    auto parentless            = shapeLocal;
    parentless.roots           = { 1 };
    const auto parentlessModel = AssembleNifModel(parentless);
    expect(parentlessModel && parentlessModel->meshes.size() == 1 &&
               parentlessModel->meshes[0].nodePath.empty() &&
               ApplyNifModelTransform(parentlessModel->meshes[0].worldTransform,
                                      { 1.0F, 0.0F, 0.0F }) ==
                   std::expected<std::array<double, 3>, NifModelError>(
                       std::array{ 10.0, 22.0, 30.0 }),
           "a parentless rooted shape uses only its retained local transform");

    auto nested = *parsed;
    nested.blocks.push_back(NifBlock{ .typeIndex = 0 });
    nested.blocks.push_back(NifBlock{ .typeIndex = 1 });
    nested.blocks.push_back(NifBlock{ .typeIndex = 2 });
    auto nestedNode       = nested.nodes[0];
    nestedNode.blockIndex = 4;
    nestedNode.children   = { 5 };
    nested.nodes.push_back(nestedNode);
    auto nestedShape       = nested.triShapes[0];
    nestedShape.blockIndex = 5;
    nestedShape.data       = 6;
    nested.triShapes.push_back(nestedShape);
    auto nestedData       = nested.triShapeData[0];
    nestedData.blockIndex = 6;
    nested.triShapeData.push_back(nestedData);
    nested.nodes[0].children               = { 4, 1 };
    nested.nodes[0].object.translation     = { 10.0F, 0.0F, 0.0F };
    nested.nodes[0].object.scale           = 5.0F;
    nested.nodes[1].object.translation     = { 0.0F, 3.0F, 0.0F };
    nested.nodes[1].object.rotation        = kRotateZPositive;
    nested.nodes[1].object.scale           = 4.0F;
    nested.triShapes[1].object.translation = { 1.0F, 0.0F, 0.0F };
    nested.triShapes[1].object.scale       = 2.0F;
    const auto nestedModel                 = AssembleNifModel(nested);
    expect(nestedModel && nestedModel->meshes.size() == 2 &&
               nestedModel->meshes[0].triShapeIndex == 1 &&
               nestedModel->meshes[0].nodePath == std::vector<std::size_t>{ 0, 1 } &&
               nestedModel->meshes[1].triShapeIndex == 0 &&
               nestedModel->meshes[1].nodePath == std::vector<std::size_t>{ 0 },
           "nested nodes preserve source-order mesh discovery and node paths");
    if (nestedModel && nestedModel->meshes.size() == 2)
    {
        const auto& nestedTransform = nestedModel->meshes[0].worldTransform;
        const auto  nestedPosition =
            ApplyNifModelTransform(nestedTransform, { 1.0F, 0.0F, 0.0F });
        expect(nestedTransform.translation == std::array{ 10.0, 35.0, 0.0 } &&
                   nestedTransform.rotation ==
                       std::array{ 0.0, 1.0, 0.0, -1.0, 0.0, 0.0, 0.0, 0.0, 1.0 } &&
                   nestedTransform.scale == 40.0 && nestedPosition &&
                   *nestedPosition == std::array{ 10.0, 75.0, 0.0 },
               "nested transforms use proved parent-local scale, rotation, and translation order");

        std::vector<NifPreviewMeshView> nestedViews;
        for (const auto& mesh : nestedModel->meshes)
        {
            const auto& geometry = nested.triShapeData[mesh.triShapeDataIndex];
            nestedViews.push_back(
                { geometry.vertexPositions, geometry.triangles, mesh.worldTransform });
        }
        const auto nestedLayout =
            CalculateNifWireframeLayout(nestedViews, 240.0F, 160.0F);
        expect(nestedLayout && nestedLayout->projection == NifProjection::Xy &&
                   nestedLayout->sourceCenter == std::array{ -7.5, 37.5 },
               "sibling and nested meshes share one transformed Preview space");
    }

    auto nullChild              = *parsed;
    nullChild.nodes[0].children = { UINT32_MAX, 1 };
    const auto nullChildModel   = AssembleNifModel(nullChild);
    expect(nullChildModel && nullChildModel->meshes.size() == 1,
           "null scene child references are ignored");

    auto materialOrder = *parsed;
    materialOrder.blocks.push_back(NifBlock{ .typeIndex = 3 });
    materialOrder.triShapes[0].object.properties = { 4, UINT32_MAX, 3 };
    const auto materialOrderModel                = AssembleNifModel(materialOrder);
    expect(materialOrderModel &&
               materialOrderModel->meshes[0].materialPropertyBlocks ==
                   std::vector<std::uint32_t>{ 4, 3 },
           "direct material properties preserve source order while nulls are ignored");

    auto selfCycle              = *parsed;
    selfCycle.nodes[0].children = { 0 };
    const auto selfCycleResult  = AssembleNifModel(selfCycle);
    expect(!selfCycleResult && selfCycleResult.error() == NifModelError::SceneCycle,
           "self-referencing scene node reports the exact cycle error");

    auto multiCycle = *parsed;
    multiCycle.blocks.push_back(NifBlock{ .typeIndex = 0 });
    auto secondNode       = multiCycle.nodes[0];
    secondNode.blockIndex = 4;
    secondNode.children   = { 0 };
    multiCycle.nodes.push_back(secondNode);
    multiCycle.nodes[0].children = { 4 };
    const auto multiCycleResult  = AssembleNifModel(multiCycle);
    expect(!multiCycleResult && multiCycleResult.error() == NifModelError::SceneCycle,
           "multi-node scene cycle terminates with the exact cycle error");

    auto wrongData              = *parsed;
    wrongData.triShapes[0].data = 3;
    const auto wrongDataResult  = AssembleNifModel(wrongData);
    expect(!wrongDataResult &&
               wrongDataResult.error() == NifModelError::WrongDataBlockType,
           "valid wrong-type shape data reference fails model assembly");

    auto invalidSelector = synthetic.bytes;
    writeU16Be(invalidSelector, synthetic.triangleIndicesOffset + 4, 3);
    const auto invalidSelectorDocument = ParseNifDocument(invalidSelector);
    expect(invalidSelectorDocument.has_value(),
           "out-of-range triangle selector remains structurally parseable");
    if (invalidSelectorDocument)
    {
        const auto invalidSelectorModel = AssembleNifModel(*invalidSelectorDocument);
        expect(!invalidSelectorModel &&
                   invalidSelectorModel.error() ==
                       NifModelError::TriangleSelectorOutOfRange,
               "out-of-range triangle selector fails at model assembly");
    }

    auto missingPositions = *parsed;
    missingPositions.triShapeData[0].vertexPositions.clear();
    const auto missingPositionsResult = AssembleNifModel(missingPositions);
    expect(!missingPositionsResult &&
               missingPositionsResult.error() == NifModelError::InconsistentGeometry,
           "missing retained positions fail model assembly");

    auto missingTriangles = *parsed;
    missingTriangles.triShapeData[0].triangles.clear();
    const auto missingTrianglesResult = AssembleNifModel(missingTriangles);
    expect(!missingTrianglesResult &&
               missingTrianglesResult.error() == NifModelError::InconsistentGeometry,
           "missing retained triangles fail model assembly");

    auto mismatchedNormals = *parsed;
    mismatchedNormals.triShapeData[0].normalVectors.pop_back();
    const auto mismatchedNormalsResult = AssembleNifModel(mismatchedNormals);
    expect(!mismatchedNormalsResult &&
               mismatchedNormalsResult.error() == NifModelError::InconsistentGeometry,
           "mismatched retained normal count fails model assembly");

    auto noNormals                       = *parsed;
    noNormals.triShapeData[0].hasNormals = 0;
    noNormals.triShapeData[0].normalVectors.clear();
    const auto noNormalsResult = AssembleNifModel(noNormals);
    expect(noNormalsResult && noNormalsResult->meshes.size() == 1,
           "geometry without a retained normal array remains supported");

    auto unsupported = *parsed;
    unsupported.blockTypes.push_back(byteString("UnsupportedSyntheticBlock"));
    unsupported.blocks.push_back(NifBlock{ .typeIndex = 4 });
    unsupported.nodes[0].children = { 4, 1 };
    const auto unsupportedResult  = AssembleNifModel(unsupported);
    expect(unsupportedResult && unsupportedResult->meshes.size() == 1,
           "valid unsupported child block types are skipped");

    auto noMesh              = *parsed;
    noMesh.nodes[0].children = {};
    const auto noMeshResult  = AssembleNifModel(noMesh);
    expect(!noMeshResult && noMeshResult.error() == NifModelError::NoSupportedMeshes,
           "rooted scene without supported geometry reports the exact no-mesh error");

    auto nonFiniteTransform = *parsed;
    nonFiniteTransform.nodes[0].object.scale =
        std::numeric_limits<float>::infinity();
    const auto nonFiniteTransformResult = AssembleNifModel(nonFiniteTransform);
    expect(!nonFiniteTransformResult &&
               nonFiniteTransformResult.error() == NifModelError::InvalidTransform,
           "non-finite retained scene transforms fail model assembly exactly");
}

auto calculateSingleMeshLayout(
    std::span<const std::array<float, 3>>         positions,
    std::span<const std::array<std::uint16_t, 3>> triangles,
    float                                         regionWidth,
    float                                         regionHeight,
    rerevved::tools::NifProjectionMode            requestedProjection =
        rerevved::tools::NifProjectionMode::Automatic)
{
    using namespace rerevved::tools;
    const std::array meshes{ NifPreviewMeshView{ positions, triangles, {} } };
    return CalculateNifWireframeLayout(
        meshes, regionWidth, regionHeight, requestedProjection);
}

std::array<float, 2> projectFloatPosition(
    const rerevved::tools::NifWireframeLayout& layout,
    const std::array<float, 3>&                position)
{
    return rerevved::tools::ProjectNifPosition(
        layout, std::array<double, 3>{ position[0], position[1], position[2] });
}

void testNifWireframePreview()
{
    using namespace rerevved::tools;

    expect(NifProjectionName(NifProjection::Xy) == "Raw XY projection" &&
               NifProjectionName(NifProjection::Xz) == "Raw XZ projection" &&
               NifProjectionName(NifProjection::Yz) == "Raw YZ projection",
           "NIF preview names every raw-axis projection exactly");
    expect(NifPreviewErrorMessage(NifPreviewError::NonFinitePosition) ==
                   "The NIF mesh contains a nonfinite vertex position." &&
               NifPreviewErrorMessage(
                   NifPreviewError::UnrepresentableTransformedPosition) ==
                   "A transformed NIF vertex position is nonfinite or unrepresentable." &&
               NifPreviewErrorMessage(NifPreviewError::EmptyProjectedBounds) ==
                   "The NIF mesh has no projected bounds to display." &&
               NifPreviewErrorMessage(NifPreviewError::DegenerateProjection) ==
                   "The NIF mesh has a degenerate raw axis projection." &&
               NifPreviewErrorMessage(NifPreviewError::UnavailableRegion) ==
                   "The NIF Preview region has no drawable area.",
           "NIF preview errors retain their exact messages");

    const auto synthetic = makeSyntheticModelNif();
    const auto document  = ParseNifDocument(synthetic.bytes);
    expect(document.has_value(), "synthetic NIF parses for wireframe preview");
    if (!document)
    {
        return;
    }
    const auto model = AssembleNifModel(*document);
    expect(model && !model->meshes.empty(), "synthetic NIF assembles for wireframe preview");
    if (!model || model->meshes.empty())
    {
        return;
    }

    const auto&      geometry = document->triShapeData[model->meshes[0].triShapeDataIndex];
    const std::array meshView{ NifPreviewMeshView{ geometry.vertexPositions,
                                                   geometry.triangles,
                                                   model->meshes[0].worldTransform } };
    const auto       layout =
        CalculateNifWireframeLayout(meshView, 240.0F, 160.0F);
    expect(layout && layout->projection == NifProjection::Xy,
           "synthetic positive mesh produces a fitted XY wireframe");
    if (layout)
    {
        const auto&      triangle = geometry.triangles[0];
        const std::array points{
            projectFloatPosition(*layout, geometry.vertexPositions[triangle[0]]),
            projectFloatPosition(*layout, geometry.vertexPositions[triangle[1]]),
            projectFloatPosition(*layout, geometry.vertexPositions[triangle[2]]),
        };
        const std::array edges{
            std::pair{ points[0], points[1] },
            std::pair{ points[1], points[2] },
            std::pair{ points[2], points[0] },
        };
        const auto visibleEdges = std::ranges::count_if(
            edges,
            [](const auto& edge)
            {
                return edge.first != edge.second;
            });
        expect(visibleEdges == 3,
               "three synthetic positions and one triangle produce three visible edges");
    }

    const std::array<std::array<std::uint16_t, 3>, 1> triangles{ { { 0, 1, 2 } } };
    const std::array                                  xyPositions{
        std::array{ 0.0F, 0.0F, 0.0F },
        std::array{ 4.0F, 0.0F, 0.0F },
        std::array{ 0.0F, 3.0F, 0.0F },
    };
    const std::array xzPositions{
        std::array{ 0.0F, 0.0F, 0.0F },
        std::array{ 4.0F, 0.0F, 0.0F },
        std::array{ 0.0F, 0.0F, 3.0F },
    };
    const std::array yzPositions{
        std::array{ 0.0F, 0.0F, 0.0F },
        std::array{ 0.0F, 4.0F, 0.0F },
        std::array{ 0.0F, 0.0F, 3.0F },
    };
    const auto xyLayout =
        calculateSingleMeshLayout(xyPositions, triangles, 200.0F, 100.0F);
    const auto xzLayout =
        calculateSingleMeshLayout(xzPositions, triangles, 200.0F, 100.0F);
    const auto yzLayout =
        calculateSingleMeshLayout(yzPositions, triangles, 200.0F, 100.0F);
    expect(xyLayout && xyLayout->projection == NifProjection::Xy && xzLayout &&
               xzLayout->projection == NifProjection::Xz && yzLayout &&
               yzLayout->projection == NifProjection::Yz,
           "dominant projected bounds deterministically select XY, XZ, and YZ");

    const std::array translatedPositions{
        std::array{ -10.0F, -20.0F, -3.0F },
        std::array{ -6.0F, -20.0F, -3.0F },
        std::array{ -10.0F, -18.0F, -3.0F },
    };
    const auto fitted =
        calculateSingleMeshLayout(translatedPositions, triangles, 200.0F, 120.0F);
    expect(fitted.has_value(), "translated negative-coordinate geometry fits successfully");
    if (fitted)
    {
        std::array<float, 2> minimum{ 200.0F, 120.0F };
        std::array<float, 2> maximum{};
        for (const auto& position : translatedPositions)
        {
            const auto projected = projectFloatPosition(*fitted, position);
            for (std::size_t axis = 0; axis < projected.size(); ++axis)
            {
                minimum[axis] = std::min(minimum[axis], projected[axis]);
                maximum[axis] = std::max(maximum[axis], projected[axis]);
            }
        }
        const auto centered =
            std::abs((minimum[0] + maximum[0]) * 0.5F - 100.0F) < 0.001F &&
            std::abs((minimum[1] + maximum[1]) * 0.5F - 60.0F) < 0.001F;
        const auto aspect = (maximum[0] - minimum[0]) / (maximum[1] - minimum[1]);
        expect(centered && std::abs(aspect - 2.0F) < 0.001F,
               "uniform fitting centers translated geometry and preserves aspect ratio");
        expect(minimum[0] >= 0.0F && minimum[1] >= 0.0F && maximum[0] <= 200.0F &&
                   maximum[1] <= 120.0F,
               "fitted wireframe stays inside the requested Preview region");
    }

    auto nonFinite  = xyPositions;
    nonFinite[2][1] = std::numeric_limits<float>::infinity();
    const auto nonFiniteResult =
        calculateSingleMeshLayout(nonFinite, triangles, 200.0F, 100.0F);
    expect(!nonFiniteResult &&
               nonFiniteResult.error() == NifPreviewError::NonFinitePosition,
           "non-finite NIF positions fail with the exact preview error");

    NifModelTransform unrepresentableTransform;
    unrepresentableTransform.scale = std::numeric_limits<double>::max();
    const std::array unrepresentableView{
        NifPreviewMeshView{ xyPositions, triangles, unrepresentableTransform }
    };
    const auto unrepresentable =
        CalculateNifWireframeLayout(unrepresentableView, 200.0F, 100.0F);
    expect(!unrepresentable &&
               unrepresentable.error() ==
                   NifPreviewError::UnrepresentableTransformedPosition,
           "unrepresentable transformed positions fail with the exact preview error");

    NifModelTransform overflowingAreaTransform;
    overflowingAreaTransform.scale = 1.0e200;
    const std::array overflowingAreaView{
        NifPreviewMeshView{ xyPositions, triangles, overflowingAreaTransform }
    };
    const auto overflowingArea =
        CalculateNifWireframeLayout(overflowingAreaView, 200.0F, 100.0F);
    expect(!overflowingArea &&
               overflowingArea.error() ==
                   NifPreviewError::UnrepresentableTransformedPosition,
           "finite transformed coordinates with overflowing projected area fail exactly");

    const std::array collinearPositions{
        std::array{ 0.0F, 0.0F, 0.0F },
        std::array{ 1.0F, 1.0F, 0.0F },
        std::array{ 2.0F, 2.0F, 0.0F },
    };
    const auto collinear =
        calculateSingleMeshLayout(collinearPositions, triangles, 200.0F, 100.0F);
    expect(!collinear && collinear.error() == NifPreviewError::DegenerateProjection,
           "collinear projected triangle fails safely");

    const std::array pointPositions{
        std::array{ 1.0F, 1.0F, 1.0F },
        std::array{ 1.0F, 1.0F, 1.0F },
        std::array{ 1.0F, 1.0F, 1.0F },
    };
    const auto point =
        calculateSingleMeshLayout(pointPositions, triangles, 200.0F, 100.0F);
    expect(!point && point.error() == NifPreviewError::EmptyProjectedBounds,
           "point-like projected bounds report the exact empty-bounds error");

    const std::span<const std::array<float, 3>> emptyPositions;
    const auto                                  empty =
        calculateSingleMeshLayout(emptyPositions, triangles, 200.0F, 100.0F);
    expect(!empty && empty.error() == NifPreviewError::EmptyProjectedBounds,
           "empty NIF positions report the exact projected-bounds error");
    const auto unavailable =
        calculateSingleMeshLayout(xyPositions, triangles, 0.0F, 100.0F);
    expect(!unavailable && unavailable.error() == NifPreviewError::UnavailableRegion,
           "non-positive Preview width fails without drawing outside its region");

    AssetDocument assembledAsset{};
    assembledAsset.nif      = *document;
    assembledAsset.nifModel = *model;
    AssetDocument assemblyFailedAsset{};
    assemblyFailedAsset.nif           = *document;
    assemblyFailedAsset.nifModelError = NifModelError::NoSupportedMeshes;
    AssetDocument parseFailedAsset{};
    parseFailedAsset.documentError =
        std::string(NifDocumentErrorMessage(NifDocumentError::Truncated));
    expect(assemblyFailedAsset.nif && !assemblyFailedAsset.nifModel &&
               assemblyFailedAsset.nifModelError &&
               assemblyFailedAsset.documentError.empty() && !parseFailedAsset.nif &&
               !parseFailedAsset.nifModelError && !parseFailedAsset.documentError.empty(),
           "NIF assembly errors remain distinct from parse errors and preserve Inspector data");

    std::vector<AssetDocument> assets;
    assets.push_back(std::move(assembledAsset));
    assets.push_back(std::move(assemblyFailedAsset));
    std::size_t selected = 0;
    expect(assets[selected].nifModel && !assets[selected].nifModelError,
           "selected assembled NIF owns its preview model state");
    selected = 1;
    expect(!assets[selected].nifModel && assets[selected].nifModelError,
           "switching NIF documents cannot reuse another preview model");
    assets.erase(assets.begin());
    expect(!assets[0].nifModel && assets[0].nifModelError,
           "closing another asset does not retain its NIF preview state");
}

void testNifWireframeNavigation()
{
    using namespace rerevved::tools;

    expect(NifProjectionModeName(NifProjectionMode::Automatic) == "Auto" &&
               NifProjectionModeName(NifProjectionMode::Xy) == "XY" &&
               NifProjectionModeName(NifProjectionMode::Xz) == "XZ" &&
               NifProjectionModeName(NifProjectionMode::Yz) == "YZ",
           "NIF preview projection controls use exact neutral labels");

    const auto synthetic = makeSyntheticModelNif();
    const auto parsed    = ParseNifDocument(synthetic.bytes);
    expect(parsed.has_value(), "synthetic NIF parses for navigation tests");
    if (!parsed)
    {
        return;
    }

    auto multiMesh = *parsed;
    multiMesh.blocks.push_back(NifBlock{ .typeIndex = 1 });
    multiMesh.blocks.push_back(NifBlock{ .typeIndex = 2 });
    auto secondShape               = multiMesh.triShapes[0];
    secondShape.blockIndex         = 4;
    secondShape.data               = 5;
    secondShape.object.translation = { 10.0F, 5.0F, 0.0F };
    multiMesh.triShapes.push_back(secondShape);
    auto secondData            = multiMesh.triShapeData[0];
    secondData.blockIndex      = 5;
    secondData.vertexPositions = {
        { 0.0F, 0.0F, 0.0F },
        { 4.0F, 0.0F, 0.0F },
        { 0.0F, 0.0F, 3.0F },
    };
    multiMesh.triShapeData.push_back(secondData);
    multiMesh.nodes[0].children = { 1, 4 };
    const auto model            = AssembleNifModel(multiMesh);
    expect(model && model->meshes.size() == 2,
           "multi-mesh synthetic NIF assembles every selectable mesh");
    if (!model || model->meshes.size() != 2)
    {
        return;
    }

    NifPreviewState navigation;
    expect(navigation.selectedMesh == 0 &&
               navigation.projection == NifProjectionMode::Automatic &&
               navigation.pan == std::array{ 0.0F, 0.0F } && navigation.zoom == 1.0,
           "new NIF documents default to the first mesh, Auto, and a fitted view");
    const std::array expectedProjections{ NifProjection::Xy, NifProjection::Xz };
    for (std::size_t index = 0; index < model->meshes.size(); ++index)
    {
        const auto selected = SelectNifPreviewMesh(
            navigation, model->meshes.size() + 1, index);
        const auto&      geometry = multiMesh.triShapeData[model->meshes[navigation.selectedMesh].triShapeDataIndex];
        const std::array selectedView{
            NifPreviewMeshView{ geometry.vertexPositions,
                                geometry.triangles,
                                model->meshes[navigation.selectedMesh].worldTransform }
        };
        const auto layout =
            CalculateNifWireframeLayout(selectedView, 240.0F, 160.0F);
        expect(selected && navigation.selectedMesh == index && layout &&
                   layout->projection == expectedProjections[index],
               "each selected mesh uses its own retained geometry");
    }

    std::vector<NifPreviewMeshView> allViews;
    for (const auto& mesh : model->meshes)
    {
        const auto& geometry = multiMesh.triShapeData[mesh.triShapeDataIndex];
        allViews.push_back(
            { geometry.vertexPositions, geometry.triangles, mesh.worldTransform });
    }
    const auto allLayout =
        CalculateNifWireframeLayout(allViews, 240.0F, 160.0F);
    const auto secondPosition = TransformNifPreviewPosition(
        allViews[1], allViews[1].positions[1]);
    expect(allLayout && allLayout->projection == NifProjection::Xy &&
               allLayout->sourceCenter == std::array{ 7.0, 2.5 } &&
               secondPosition && *secondPosition == std::array{ 14.0, 5.0, 0.0 },
           "All meshes fits combined transformed bounds while individual meshes use their transforms");

    PanNifPreview(navigation, 2.0F, 3.0F);
    ZoomNifPreview(navigation, 1.0F);
    const auto selectedAll = SelectNifPreviewMesh(
        navigation, model->meshes.size() + 1, model->meshes.size());
    expect(selectedAll && navigation.selectedMesh == model->meshes.size() &&
               navigation.pan == std::array{ 0.0F, 0.0F } && navigation.zoom == 1.0,
           "All meshes is an explicit fitted selection after every individual mesh");

    navigation.pan  = { 9.0F, -4.0F };
    navigation.zoom = 3.0;
    const auto invalidSelection =
        SelectNifPreviewMesh(navigation, model->meshes.size() + 1, 99);
    expect(!invalidSelection && navigation.selectedMesh == 0 &&
               navigation.pan == std::array{ 0.0F, 0.0F } && navigation.zoom == 1.0,
           "invalid mesh selection deterministically recovers to the first fitted mesh");

    const std::array<std::array<float, 3>, 3> fullAxisPositions{
        std::array{ 0.0F, 0.0F, 0.0F },
        std::array{ 4.0F, 1.0F, 2.0F },
        std::array{ 1.0F, 3.0F, 4.0F },
    };
    const std::array<std::array<std::uint16_t, 3>, 1> triangle{
        std::array<std::uint16_t, 3>{ 0, 1, 2 },
    };
    const auto automatic = calculateSingleMeshLayout(fullAxisPositions,
                                                     triangle,
                                                     240.0F,
                                                     160.0F,
                                                     NifProjectionMode::Automatic);
    const auto xy        = calculateSingleMeshLayout(
        fullAxisPositions, triangle, 240.0F, 160.0F, NifProjectionMode::Xy);
    const auto xz = calculateSingleMeshLayout(
        fullAxisPositions, triangle, 240.0F, 160.0F, NifProjectionMode::Xz);
    const auto yz = calculateSingleMeshLayout(
        fullAxisPositions, triangle, 240.0F, 160.0F, NifProjectionMode::Yz);
    expect(automatic && automatic->projection == NifProjection::Xz && xy &&
               xy->projection == NifProjection::Xy && xz &&
               xz->projection == NifProjection::Xz && yz &&
               yz->projection == NifProjection::Yz,
           "Auto and explicit XY, XZ, and YZ projections are deterministic");

    if (xy)
    {
        const auto fittedScale  = xy->scale;
        const auto fittedCenter = xy->targetCenter;
        PanNifPreview(navigation, 12.0F, -7.0F);
        ZoomNifPreview(navigation, 1.0F);
        const auto viewed = ApplyNifPreviewView(*xy, navigation);
        expect(std::abs(viewed.scale - fittedScale * 1.2) < 0.001 &&
                   std::abs(viewed.targetCenter[0] - fittedCenter[0] - 12.0) < 0.001 &&
                   std::abs(viewed.targetCenter[1] - fittedCenter[1] + 7.0) < 0.001,
               "NIF preview pan and wheel zoom adjust only the fitted view");
        ResetNifPreviewView(navigation);
        const auto reset = ApplyNifPreviewView(*xy, navigation);
        expect(reset.scale == fittedScale && reset.targetCenter == fittedCenter,
               "Fit/Reset restores the deterministic fitted view");
    }

    PanNifPreview(navigation, 5.0F, 6.0F);
    ZoomNifPreview(navigation, 2.0F);
    SelectNifPreviewProjection(navigation, NifProjectionMode::Yz);
    expect(navigation.projection == NifProjectionMode::Yz &&
               navigation.pan == std::array{ 0.0F, 0.0F } && navigation.zoom == 1.0,
           "projection changes restore the fitted view");
    PanNifPreview(navigation, 3.0F, 4.0F);
    SelectNifPreviewProjection(navigation, NifProjectionMode::Yz);
    expect(navigation.pan == std::array{ 3.0F, 4.0F },
           "reselecting the active projection preserves navigation state");
    (void)SelectNifPreviewMesh(navigation, model->meshes.size() + 1, 1);
    expect(navigation.selectedMesh == 1 &&
               navigation.pan == std::array{ 0.0F, 0.0F } && navigation.zoom == 1.0,
           "mesh changes restore the fitted view");

    AssetDocument firstAsset{};
    firstAsset.nif             = multiMesh;
    firstAsset.nifModel        = *model;
    firstAsset.nifPreview      = navigation;
    firstAsset.nifPreview.pan  = { 8.0F, 9.0F };
    firstAsset.nifPreview.zoom = 2.0;
    AssetDocument secondAsset{};
    secondAsset.nif                     = multiMesh;
    secondAsset.nifModel                = *model;
    secondAsset.nifPreview.selectedMesh = 0;
    secondAsset.nifPreview.projection   = NifProjectionMode::Xy;
    secondAsset.nifPreview.pan          = { -3.0F, 1.0F };
    secondAsset.nifPreview.zoom         = 0.5;
    expect(firstAsset.nifPreview.selectedMesh == 1 &&
               firstAsset.nifPreview.projection == NifProjectionMode::Yz &&
               firstAsset.nifPreview.pan == std::array{ 8.0F, 9.0F } &&
               firstAsset.nifPreview.zoom == 2.0 &&
               secondAsset.nifPreview.selectedMesh == 0 &&
               secondAsset.nifPreview.projection == NifProjectionMode::Xy &&
               secondAsset.nifPreview.pan == std::array{ -3.0F, 1.0F },
           "open NIF documents retain independent mesh, projection, pan, and zoom state");
    std::vector<AssetDocument> assets;
    assets.push_back(std::move(firstAsset));
    assets.push_back(std::move(secondAsset));
    assets.erase(assets.begin());
    expect(assets[0].nifPreview.selectedMesh == 0 &&
               assets[0].nifPreview.projection == NifProjectionMode::Xy &&
               assets[0].nifPreview.pan == std::array{ -3.0F, 1.0F } &&
               assets[0].nifPreview.zoom == 0.5,
           "closing or switching NIF documents cannot retain another document's view state");
}

std::vector<std::byte> makeFpk()
{
    constexpr std::size_t  kTableEnd = 14 + 24 + 24;
    std::vector<std::byte> bytes;
    appendU32Le(bytes, 6);
    bytes.insert(bytes.end(), { std::byte{ 'F' }, std::byte{ 'P' }, std::byte{ 'K' }, std::byte{ '_' }, std::byte{ 0x12 }, std::byte{ 0x34 } });
    appendU32Le(bytes, 2);

    appendU32Le(bytes, 3);
    bytes.insert(bytes.end(), 4, std::byte{ 0x41 });
    bytes.insert(bytes.end(), { std::byte{ 0x01 }, std::byte{ 0x02 }, std::byte{ 0x03 }, std::byte{ 0x04 }, std::byte{ 0x05 }, std::byte{ 0x06 }, std::byte{ 0x07 }, std::byte{ 0x08 } });
    appendU32Le(bytes, 5);
    appendU32Le(bytes, static_cast<std::uint32_t>(kTableEnd));

    appendU32Le(bytes, 4);
    bytes.insert(bytes.end(), 4, std::byte{ 0x42 });
    bytes.insert(bytes.end(), { std::byte{ 0x11 }, std::byte{ 0x12 }, std::byte{ 0x13 }, std::byte{ 0x14 }, std::byte{ 0x15 }, std::byte{ 0x16 }, std::byte{ 0x17 }, std::byte{ 0x18 } });
    appendU32Le(bytes, 3);
    appendU32Le(bytes, static_cast<std::uint32_t>(kTableEnd + 5));

    bytes.insert(bytes.end(), { std::byte{ 0x31 }, std::byte{ 0x32 }, std::byte{ 0x33 }, std::byte{ 0x34 }, std::byte{ 0x35 }, std::byte{ 0x61 }, std::byte{ 0x62 }, std::byte{ 0x63 } });
    return bytes;
}

std::vector<std::byte>
makeFpkWithPayloads(std::span<const std::vector<std::byte>> payloads)
{
    const auto             kTableEnd = 14U + payloads.size() * 20U;
    std::vector<std::byte> bytes;
    appendU32Le(bytes, 6);
    bytes.insert(bytes.end(),
                 { std::byte{ 'F' },
                   std::byte{ 'P' },
                   std::byte{ 'K' },
                   std::byte{ '_' },
                   std::byte{ 0x12 },
                   std::byte{ 0x34 } });
    appendU32Le(bytes, static_cast<std::uint32_t>(payloads.size()));

    std::size_t payloadOffset = kTableEnd;
    for (std::size_t index = 0; index < payloads.size(); ++index)
    {
        appendU32Le(bytes, 0);
        appendU32Le(bytes, static_cast<std::uint32_t>(0x1000U + index));
        appendU32Le(bytes, static_cast<std::uint32_t>(0x2000U + index));
        appendU32Le(bytes, static_cast<std::uint32_t>(payloads[index].size()));
        appendU32Le(bytes, static_cast<std::uint32_t>(payloadOffset));
        payloadOffset += payloads[index].size();
    }
    for (const auto& payload : payloads)
    {
        bytes.insert(bytes.end(), payload.begin(), payload.end());
    }
    return bytes;
}

void testCloseOpenedArchiveEntry()
{
    using rerevved::tools::ArchiveExplorerState;
    using rerevved::tools::CloseOpenedArchiveEntry;
    using rerevved::tools::FpkEntryFormat;
    using rerevved::tools::OpenFpkEntryDocument;
    using rerevved::tools::ParseFpkDocument;

    const std::array payloads{ makeGfx() };
    const auto       archiveBytes   = makeFpkWithPayloads(payloads);
    const auto       firstDocument  = ParseFpkDocument(archiveBytes);
    const auto       secondDocument = ParseFpkDocument(archiveBytes);
    expect(firstDocument && secondDocument,
           "independent synthetic archives for embedded close are valid");
    if (!firstDocument || !secondDocument)
    {
        return;
    }

    const auto firstOpen =
        OpenFpkEntryDocument(*firstDocument, 0, FpkEntryFormat::Gfx);
    const auto secondOpen =
        OpenFpkEntryDocument(*secondDocument, 0, FpkEntryFormat::Gfx);
    expect(firstOpen && secondOpen,
           "independent synthetic embedded entries open before close");
    if (!firstOpen || !secondOpen)
    {
        return;
    }

    ArchiveExplorerState firstState;
    firstState.selectedEntry    = 0;
    firstState.requestedEntry   = 1;
    firstState.selectedFormat   = FpkEntryFormat::Gfx;
    firstState.openedDocument   = *firstOpen;
    firstState.openError        = "retained open error";
    firstState.navigationError  = "retained navigation error";
    firstState.metadataResult   = "retained metadata result";
    firstState.extractionResult = "retained extraction result";
    firstState.extractionPath.fill('X');

    ArchiveExplorerState secondState;
    secondState.selectedEntry  = 0;
    secondState.requestedEntry = 1;
    secondState.selectedFormat = FpkEntryFormat::Gfx;
    secondState.openedDocument = *secondOpen;

    const auto selectedEntryBefore     = firstState.selectedEntry;
    const auto requestedEntryBefore    = firstState.requestedEntry;
    const auto selectedFormatBefore    = firstState.selectedFormat;
    const auto navigationErrorBefore   = firstState.navigationError;
    const auto metadataResultBefore    = firstState.metadataResult;
    const auto extractionResultBefore  = firstState.extractionResult;
    const auto extractionPathBefore    = firstState.extractionPath;
    const auto unrelatedStatePreserved = [&]
    {
        return firstState.selectedEntry == selectedEntryBefore &&
               firstState.requestedEntry == requestedEntryBefore &&
               firstState.selectedFormat == selectedFormatBefore &&
               firstState.openError.empty() && firstState.presentationError.empty() &&
               firstState.navigationError == navigationErrorBefore &&
               firstState.metadataResult == metadataResultBefore &&
               firstState.extractionResult == extractionResultBefore &&
               firstState.extractionPath == extractionPathBefore;
    };

    CloseOpenedArchiveEntry(firstState);
    expect(!firstState.openedDocument && !firstState.nifPreview,
           "closing an embedded entry releases its opened document and preview state");
    expect(unrelatedStatePreserved(),
           "closing an embedded entry preserves every unrelated archive field");
    expect(firstDocument->bytes == archiveBytes && secondDocument->bytes == archiveBytes,
           "closing an embedded entry preserves each parent archive byte");
    expect(secondState.openedDocument && secondState.selectedEntry == 0 &&
               secondState.selectedFormat == FpkEntryFormat::Gfx,
           "closing one embedded entry leaves another archive state independent");

    CloseOpenedArchiveEntry(firstState);
    expect(!firstState.openedDocument && unrelatedStatePreserved(),
           "closing with no opened embedded document is harmless");

    const auto reopened = OpenFpkEntryDocument(*firstDocument,
                                               *firstState.selectedEntry,
                                               *firstState.selectedFormat);
    expect(reopened && reopened->entryIndex == *firstState.selectedEntry &&
               reopened->format == firstState.selectedFormat,
           "retained selection and explicit format reopen the same embedded entry");
}

void testEmbeddedNifWireframePreview()
{
    using namespace rerevved::tools;

    auto synthetic = makeSyntheticModelNif();
    writeNifF32(synthetic.bytes, synthetic.shapeTransformOffset, 10.0F);
    writeNifF32(synthetic.bytes, synthetic.shapeTransformOffset + 4, 5.0F);
    writeNifF32(synthetic.bytes, synthetic.shapeTransformOffset + 48, 2.0F);
    auto noMesh = synthetic.bytes;
    writeU32Be(noMesh, synthetic.rootReferenceOffset, UINT32_MAX);
    const std::array<std::vector<std::byte>, 5> payloads{
        synthetic.bytes,
        synthetic.bytes,
        noMesh,
        std::vector{ std::byte{ 'B' }, std::byte{ 'A' }, std::byte{ 'D' } },
        makeGfx(),
    };
    const auto archiveBytes  = makeFpkWithPayloads(payloads);
    const auto firstArchive  = ParseFpkDocument(archiveBytes);
    const auto secondArchive = ParseFpkDocument(archiveBytes);
    expect(firstArchive && secondArchive,
           "independent synthetic archives parse for embedded NIF preview");
    if (!firstArchive || !secondArchive)
    {
        return;
    }

    ArchiveExplorerState firstState;
    (void)SelectArchiveEntry(firstState, payloads.size(), 1);
    firstState.selectedFormat   = FpkEntryFormat::Nif;
    firstState.metadataResult   = "retained metadata";
    firstState.extractionResult = "retained extraction";
    firstState.navigationError  = "retained navigation";
    firstState.extractionPath.fill('P');
    OpenSelectedArchiveEntryInMemory(*firstArchive, firstState);
    expect(firstState.openedDocument && firstState.nifPreview &&
               firstState.nifPreview->model &&
               !firstState.nifPreview->assemblyError && firstState.openError.empty(),
           "explicitly opened embedded NIF assembles a wireframe preview model");
    expect(firstState.nifPreview &&
               firstState.nifPreview->navigation.selectedMesh == 0 &&
               firstState.nifPreview->navigation.projection ==
                   NifProjectionMode::Automatic &&
               firstState.nifPreview->navigation.pan == std::array{ 0.0F, 0.0F } &&
               firstState.nifPreview->navigation.zoom == 1.0,
           "new embedded NIF preview starts on the first mesh with fitted Auto state");
    if (firstState.nifPreview && firstState.nifPreview->model)
    {
        const auto& previewDocument =
            std::get<NifDocument>(firstState.openedDocument->data);
        const auto& previewMesh = firstState.nifPreview->model->meshes[0];
        const auto& previewGeometry =
            previewDocument.triShapeData[previewMesh.triShapeDataIndex];
        const auto embeddedPosition = ApplyNifModelTransform(
            previewMesh.worldTransform, previewGeometry.vertexPositions[1]);
        expect(embeddedPosition && *embeddedPosition == std::array{ 12.0, 5.0, 0.0 },
               "embedded NIF preview applies the retained shape transform");
        const auto selectedAll = SelectNifPreviewMesh(
            firstState.nifPreview->navigation,
            firstState.nifPreview->model->meshes.size() + 1,
            firstState.nifPreview->model->meshes.size());
        expect(selectedAll && firstState.nifPreview->navigation.selectedMesh == 1,
               "embedded NIF Preview exposes the shared All meshes selection");
    }

    firstState.nifPreview->navigation.projection = NifProjectionMode::Yz;
    firstState.nifPreview->navigation.pan        = { 7.0F, -3.0F };
    firstState.nifPreview->navigation.zoom       = 2.0;

    ArchiveExplorerState secondState;
    (void)SelectArchiveEntry(secondState, payloads.size(), 1);
    secondState.selectedFormat = FpkEntryFormat::Nif;
    OpenSelectedArchiveEntryInMemory(*secondArchive, secondState);
    expect(secondState.nifPreview && secondState.nifPreview->model &&
               secondState.nifPreview->navigation.selectedMesh == 0 &&
               secondState.nifPreview->navigation.projection ==
                   NifProjectionMode::Automatic &&
               secondState.nifPreview->navigation.pan == std::array{ 0.0F, 0.0F } &&
               secondState.nifPreview->navigation.zoom == 1.0 &&
               firstState.nifPreview->navigation.selectedMesh == 1 &&
               firstState.nifPreview->navigation.projection == NifProjectionMode::Yz &&
               firstState.nifPreview->navigation.pan == std::array{ 7.0F, -3.0F } &&
               firstState.nifPreview->navigation.zoom == 2.0,
           "separate archives retain independent embedded NIF preview state");

    const auto selectedBefore        = firstState.selectedEntry;
    const auto requestedBefore       = firstState.requestedEntry;
    const auto formatBefore          = firstState.selectedFormat;
    const auto metadataBefore        = firstState.metadataResult;
    const auto extractionBefore      = firstState.extractionResult;
    const auto extractionPathBefore  = firstState.extractionPath;
    const auto navigationErrorBefore = firstState.navigationError;
    const auto sourceBefore          = firstArchive->bytes;
    CloseOpenedArchiveEntry(firstState);
    expect(!firstState.openedDocument && !firstState.nifPreview &&
               firstState.selectedEntry == selectedBefore &&
               firstState.requestedEntry == requestedBefore &&
               firstState.selectedFormat == formatBefore &&
               firstState.metadataResult == metadataBefore &&
               firstState.extractionResult == extractionBefore &&
               firstState.extractionPath == extractionPathBefore &&
               firstState.navigationError == navigationErrorBefore &&
               firstArchive->bytes == sourceBefore,
           "closing embedded NIF clears only opened preview ownership and preserves archive state");
    expect(secondState.openedDocument && secondState.nifPreview &&
               secondState.nifPreview->model,
           "closing one archive preview leaves another archive fully independent");

    OpenSelectedArchiveEntryInMemory(*firstArchive, firstState);
    expect(firstState.openedDocument && firstState.nifPreview &&
               firstState.nifPreview->model &&
               firstState.nifPreview->navigation.selectedMesh == 0 &&
               firstState.nifPreview->navigation.projection ==
                   NifProjectionMode::Automatic &&
               firstState.nifPreview->navigation.pan == std::array{ 0.0F, 0.0F } &&
               firstState.nifPreview->navigation.zoom == 1.0,
           "closed embedded NIF reopens immediately with deterministic preview state");

    firstState.nifPreview->navigation.projection = NifProjectionMode::Xz;
    firstState.nifPreview->navigation.pan        = { 4.0F, 5.0F };
    firstState.nifPreview->navigation.zoom       = 3.0;
    (void)SelectArchiveEntry(firstState, payloads.size(), 2);
    expect(!firstState.openedDocument && !firstState.nifPreview &&
               firstState.selectedEntry == 1,
           "selecting another archive entry removes the previous embedded preview state");
    firstState.selectedFormat = FpkEntryFormat::Nif;
    OpenSelectedArchiveEntryInMemory(*firstArchive, firstState);
    expect(firstState.openedDocument && firstState.nifPreview &&
               firstState.nifPreview->model &&
               firstState.nifPreview->navigation.selectedMesh == 0 &&
               firstState.nifPreview->navigation.projection ==
                   NifProjectionMode::Automatic &&
               firstState.nifPreview->navigation.pan == std::array{ 0.0F, 0.0F } &&
               firstState.nifPreview->navigation.zoom == 1.0,
           "opening a different embedded NIF initializes a fresh fitted Auto view");

    (void)SelectArchiveEntry(firstState, payloads.size(), 4);
    firstState.selectedFormat   = FpkEntryFormat::Nif;
    firstState.metadataResult   = "parse metadata";
    firstState.extractionResult = "parse extraction";
    OpenSelectedArchiveEntryInMemory(*firstArchive, firstState);
    expect(!firstState.openedDocument && firstState.nifPreview &&
               !firstState.nifPreview->model &&
               !firstState.nifPreview->assemblyError &&
               firstState.openError == "The NIF file is truncated." &&
               firstState.metadataResult == "parse metadata" &&
               firstState.extractionResult == "parse extraction",
           "embedded NIF parse failure remains distinct and preserves Archive Explorer state");

    (void)SelectArchiveEntry(firstState, payloads.size(), 3);
    firstState.selectedFormat   = FpkEntryFormat::Nif;
    firstState.metadataResult   = "assembly metadata";
    firstState.extractionResult = "assembly extraction";
    OpenSelectedArchiveEntryInMemory(*firstArchive, firstState);
    expect(firstState.openedDocument && firstState.nifPreview &&
               !firstState.nifPreview->model &&
               firstState.nifPreview->assemblyError ==
                   NifModelError::NoSupportedMeshes &&
               firstState.openError.empty() &&
               firstState.metadataResult == "assembly metadata" &&
               firstState.extractionResult == "assembly extraction",
           "embedded NIF assembly failure remains distinct from parse failure");

    ArchiveExplorerState explicitGfxState;
    (void)SelectArchiveEntry(explicitGfxState, payloads.size(), 1);
    explicitGfxState.selectedFormat = FpkEntryFormat::Gfx;
    OpenSelectedArchiveEntryInMemory(*firstArchive, explicitGfxState);
    expect(!explicitGfxState.openedDocument && !explicitGfxState.nifPreview &&
               explicitGfxState.openError == "The file does not have a valid GFX signature.",
           "a selected NIF payload is not previewed without explicit NIF routing");

    ArchiveExplorerState validGfxState;
    (void)SelectArchiveEntry(validGfxState, payloads.size(), 5);
    validGfxState.selectedFormat = FpkEntryFormat::Gfx;
    OpenSelectedArchiveEntryInMemory(*firstArchive, validGfxState);
    expect(validGfxState.openedDocument && !validGfxState.nifPreview &&
               std::holds_alternative<GfxDocument>(validGfxState.openedDocument->data),
           "non-NIF embedded preview routing remains unchanged");

    AssetDocument directAsset;
    const auto    directDocument = ParseNifDocument(synthetic.bytes);
    expect(directDocument.has_value(), "direct NIF regression fixture parses");
    if (directDocument)
    {
        directAsset.nif        = *directDocument;
        const auto directModel = AssembleNifModel(*directDocument);
        if (directModel)
        {
            directAsset.nifModel = *directModel;
        }
        directAsset.nifPreview.projection = NifProjectionMode::Xz;
        directAsset.nifPreview.pan        = { 11.0F, 12.0F };
        directAsset.nifPreview.zoom       = 1.5;
        CloseOpenedArchiveEntry(secondState);
        const auto directPosition = directAsset.nifModel
                                        ? ApplyNifModelTransform(
                                              directAsset.nifModel->meshes[0].worldTransform,
                                              directAsset.nif->triShapeData[0]
                                                  .vertexPositions[1])
                                        : std::expected<std::array<double, 3>, NifModelError>(
                                              std::unexpected(
                                                  NifModelError::InvalidTransform));
        expect(directAsset.nif && directAsset.nifModel && directPosition &&
                   *directPosition == std::array{ 12.0, 5.0, 0.0 } &&
                   directAsset.nifPreview.projection == NifProjectionMode::Xz &&
                   directAsset.nifPreview.pan == std::array{ 11.0F, 12.0F } &&
                   directAsset.nifPreview.zoom == 1.5,
               "direct and embedded NIFs share transformed preview behavior and isolated state");
    }

    expect(firstArchive->bytes == archiveBytes && secondArchive->bytes == archiveBytes,
           "embedded NIF preview lifecycle preserves every retained archive byte");
}

void removeTestFile(const std::filesystem::path& path);

void testInitialArchiveSelection()
{
    using rerevved::tools::ArchiveExplorerState;
    using rerevved::tools::AssetDocument;
    using rerevved::tools::ParseFpkDocument;
    using rerevved::tools::SelectArchiveEntry;
    using rerevved::tools::SelectInitialArchiveEntry;

    const std::array<std::vector<std::byte>, 1> onePayload{
        std::vector{ std::byte{ 0x10 }, std::byte{ 0x20 }, std::byte{ 0x30 } }
    };
    const auto oneBytes    = makeFpkWithPayloads(onePayload);
    const auto oneDocument = ParseFpkDocument(oneBytes);
    expect(oneDocument.has_value(), "one-entry synthetic FPK document is valid");

    ArchiveExplorerState oneState;
    if (oneDocument)
    {
        SelectInitialArchiveEntry(oneState, *oneDocument);
    }
    expect(oneState.selectedEntry && *oneState.selectedEntry == 0 &&
               oneState.requestedEntry == 1,
           "one-entry archive initially selects displayed entry 1");
    expect(!oneState.openedDocument && oneState.openError.empty() &&
               oneState.extractionResult.empty() && oneState.metadataResult.empty() &&
               std::ranges::all_of(oneState.extractionPath,
                                   [](char value)
                                   {
                                       return value == '\0';
                                   }),
           "initial archive selection has no embedded-open or extraction side effect");

    const std::array<std::vector<std::byte>, 3> multiplePayloads{
        std::vector{ std::byte{ 0x11 } },
        std::vector{ std::byte{ 0x21 }, std::byte{ 0x22 } },
        std::vector{ std::byte{ 0x31 }, std::byte{ 0x32 }, std::byte{ 0x33 } },
    };
    const auto multipleBytes    = makeFpkWithPayloads(multiplePayloads);
    const auto multipleDocument = ParseFpkDocument(multipleBytes);
    expect(multipleDocument.has_value(), "multi-entry synthetic FPK document is valid");

    AssetDocument firstAsset{};
    if (multipleDocument)
    {
        firstAsset.fpk = *multipleDocument;
        SelectInitialArchiveEntry(firstAsset.archive, *firstAsset.fpk);
    }
    auto& multipleState = firstAsset.archive;
    expect(multipleState.selectedEntry && *multipleState.selectedEntry == 0 &&
               multipleState.requestedEntry == 1,
           "multi-entry archive initially selects displayed entry 1");

    const std::array<std::vector<std::byte>, 0> emptyPayloads{};
    const auto                                  emptyBytes    = makeFpkWithPayloads(emptyPayloads);
    const auto                                  emptyDocument = ParseFpkDocument(emptyBytes);
    expect(emptyDocument.has_value() && emptyDocument->index.entries.empty(),
           "empty synthetic FPK document is valid");
    ArchiveExplorerState emptyState;
    if (emptyDocument)
    {
        SelectInitialArchiveEntry(emptyState, *emptyDocument);
    }
    expect(!emptyState.selectedEntry && !emptyState.openedDocument,
           "empty archive initialization retains no selection");

    const auto          failedDocument = ParseFpkDocument(std::span(oneBytes).first(13));
    const AssetDocument failedAsset{};
    expect(!failedDocument && !failedAsset.fpk && !failedAsset.archive.selectedEntry &&
               !failedAsset.archive.openedDocument,
           "failed archive initialization creates no document, selection, or opened entry");

    if (!multipleDocument)
    {
        return;
    }

    expect(SelectArchiveEntry(multipleState, multipleDocument->index.entries.size(), 3),
           "user can change the initial archive selection");
    multipleState.openedDocument.emplace();
    multipleState.extractionResult = "current extraction result";
    SelectInitialArchiveEntry(multipleState, *multipleDocument);
    expect(multipleState.selectedEntry && *multipleState.selectedEntry == 2 &&
               multipleState.requestedEntry == 3 && multipleState.openedDocument &&
               multipleState.extractionResult == "current extraction result",
           "reopening an initialized archive preserves later user state");

    const auto secondDocument = ParseFpkDocument(multipleBytes);
    expect(secondDocument.has_value(), "second synthetic FPK document is independently valid");
    AssetDocument secondAsset{};
    if (secondDocument)
    {
        secondAsset.fpk = *secondDocument;
        SelectInitialArchiveEntry(secondAsset.archive, *secondAsset.fpk);
    }
    auto& secondState = secondAsset.archive;
    expect(secondState.selectedEntry && *secondState.selectedEntry == 0 &&
               secondAsset.fpk && !secondState.openedDocument &&
               secondState.extractionResult.empty(),
           "a second archive owns independent initial state");
    expect(SelectArchiveEntry(secondState, multipleDocument->index.entries.size(), 2),
           "second archive selection can change independently");
    expect(multipleState.selectedEntry && *multipleState.selectedEntry == 2 &&
               multipleState.openedDocument && secondState.selectedEntry &&
               *secondState.selectedEntry == 1 && !secondState.openedDocument,
           "simultaneously opened archives retain independent selection and document state");
}

void testFpkIndex()
{
    using rerevved::tools::FpkIndexError;
    using rerevved::tools::ParseFpkIndex;

    const auto bytes = makeFpk();
    const auto index = ParseFpkIndex(bytes);
    expect(index.has_value(), "FPK index parsing succeeds");
    if (index)
    {
        expect(index->version == 6, "FPK version is preserved");
        expect(index->headerUnknown ==
                   std::array{ std::byte{ 0x12 }, std::byte{ 0x34 } },
               "FPK header unknown bytes are preserved");
        expect(index->entries.size() == 2, "FPK entry count is preserved");
        expect(index->entries[0].offset == 62 && index->entries[0].size == 5,
               "FPK entry byte range is preserved");
        expect(index->entries[0].unknown0 ==
                   std::array{ std::byte{ 0x01 }, std::byte{ 0x02 }, std::byte{ 0x03 }, std::byte{ 0x04 } },
               "FPK entry unknown bytes are preserved");
        expect(index->entries[0].unknown1 ==
                   std::array{ std::byte{ 0x05 }, std::byte{ 0x06 }, std::byte{ 0x07 }, std::byte{ 0x08 } },
               "FPK second entry unknown bytes are preserved");
    }

    expect(!ParseFpkIndex(std::span(bytes).first(13)), "truncated FPK header is rejected");

    auto oversizedCount = bytes;
    writeU32Le(oversizedCount, 10, std::numeric_limits<std::uint32_t>::max());
    expect(!ParseFpkIndex(oversizedCount),
           "FPK count arithmetic exceeding the bounded table is rejected");

    auto truncatedRecord = bytes;
    writeU32Le(truncatedRecord, 14, std::numeric_limits<std::uint32_t>::max());
    expect(!ParseFpkIndex(truncatedRecord), "truncated variable FPK record is rejected");

    auto outOfRange = bytes;
    writeU32Le(outOfRange, 34, static_cast<std::uint32_t>(bytes.size() + 1));
    const auto outOfRangeResult = ParseFpkIndex(outOfRange);
    expect(!outOfRangeResult && outOfRangeResult.error() == FpkIndexError::EntryOutOfRange,
           "out-of-range FPK entry is rejected");

    auto rangeOverflow = bytes;
    writeU32Le(rangeOverflow, 30, 32);
    writeU32Le(rangeOverflow, 34, 0xFFFFFFF0U);
    const auto rangeOverflowResult = ParseFpkIndex(rangeOverflow);
    expect(!rangeOverflowResult &&
               rangeOverflowResult.error() == FpkIndexError::EntryRangeOverflow,
           "FPK offset and size overflow is rejected");
}

void testFpkEntryIdentification()
{
    using rerevved::tools::FpkEntryKind;
    using rerevved::tools::InspectFpkEntryKind;
    using rerevved::tools::ParseFpkDocument;

    const auto makePayload = [](std::string_view text)
    {
        const auto bytes = std::as_bytes(std::span(text.data(), text.size()));
        return std::vector<std::byte>(bytes.begin(), bytes.end());
    };
    const std::array payloads{
        makePayload("DDS incomplete"),
        makePayload("GFX incomplete"),
        makePayload("CFX incomplete"),
        makePayload("ID3 incomplete"),
        makePayload("Gamebryo File Format, Version 20.3.0.9\nincomplete"),
        makePayload("prefix DDS "),
        std::vector<std::byte>(rerevved::tools::kMapEnvelopeSize),
        std::vector{ std::byte{ 0xFF }, std::byte{ 0xFB }, std::byte{ 0x90 } },
    };
    const auto archiveBytes = makeFpkWithPayloads(payloads);
    const auto sourceBefore = archiveBytes;
    auto       document     = ParseFpkDocument(archiveBytes);
    expect(document.has_value(), "synthetic FPK for entry identification parses");
    if (!document)
    {
        return;
    }

    const std::array expected{
        FpkEntryKind::DdsTexture,
        FpkEntryKind::GfxMovie,
        FpkEntryKind::GfxMovie,
        FpkEntryKind::Id3TaggedAudio,
        FpkEntryKind::NifContainer,
        FpkEntryKind::Unknown,
        FpkEntryKind::Unknown,
        FpkEntryKind::Unknown,
    };
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        const auto kind = InspectFpkEntryKind(*document, index);
        expect(kind && *kind == expected[index],
               "FPK entry identification uses only exact supported leading signatures");
    }
    expect(document->bytes == archiveBytes && archiveBytes == sourceBefore,
           "FPK entry identification leaves archive bytes unchanged");

    const auto invalidSelection = InspectFpkEntryKind(*document, payloads.size());
    expect(!invalidSelection &&
               invalidSelection.error() == "The selected FPK entry does not exist.",
           "FPK entry identification rejects an invalid selection");

    document->index.entries[0].offset = std::numeric_limits<std::uint32_t>::max();
    document->index.entries[0].size   = 2;
    const auto invalidRange           = InspectFpkEntryKind(*document, 0);
    expect(!invalidRange &&
               invalidRange.error() == "The selected FPK entry range is invalid.",
           "FPK entry identification revalidates the selected range");
}

void testFpkDocument()
{
    using rerevved::tools::DdsDocument;
    using rerevved::tools::FpkEntryFormat;
    using rerevved::tools::GfxDocument;
    using rerevved::tools::LoadFpkDocument;
    using rerevved::tools::MapDocument;
    using rerevved::tools::Mp3Document;
    using rerevved::tools::NifDocument;
    using rerevved::tools::OpenFpkEntryDocument;
    using rerevved::tools::ParseFpkDocument;
    using rerevved::tools::test::kSyntheticMp3;

    const std::array ddsPixels{
        std::byte{ 0x10 }, std::byte{ 0x20 }, std::byte{ 0x30 }, std::byte{ 0xFF }
    };
    auto       dds = makePreviewDds(1,
                                    1,
                                    0x00FF0000U,
                                    0x0000FF00U,
                                    0x000000FFU,
                                    0xFF000000U,
                                    ddsPixels);
    auto       gfx = makeGfx();
    auto       nif = makeNif().bytes;
    const auto mp3Bytes =
        std::as_bytes(std::span(kSyntheticMp3.data(), kSyntheticMp3.size()));
    std::vector<std::byte> mp3(mp3Bytes.begin(), mp3Bytes.end());
    std::vector<std::byte> map(rerevved::tools::kMapEnvelopeSize);
    std::ranges::fill(map.begin() +
                          static_cast<std::ptrdiff_t>(rerevved::tools::kMapCoreSize),
                      map.end(),
                      std::byte{ 0xFF });

    const std::array payloads{
        std::move(dds), std::move(gfx), std::move(nif), std::move(mp3), std::move(map)
    };
    const auto archiveBytes = makeFpkWithPayloads(payloads);
    const auto sourceBefore = archiveBytes;
    auto       document     = ParseFpkDocument(archiveBytes);
    expect(document.has_value(), "FPK document retains a validated synthetic archive");
    expect(archiveBytes == sourceBefore, "FPK document parsing leaves source bytes unchanged");
    if (!document)
    {
        return;
    }

    expect(document->bytes == archiveBytes, "FPK document retains exact validated source bytes");
    expect(document->index.entries.size() == payloads.size(),
           "FPK document retains the bounded entry index");

    const std::array formats{
        FpkEntryFormat::Dds,
        FpkEntryFormat::Gfx,
        FpkEntryFormat::Nif,
        FpkEntryFormat::Mp3,
        FpkEntryFormat::Map,
    };
    for (std::size_t index = 0; index < formats.size(); ++index)
    {
        const auto opened = OpenFpkEntryDocument(*document, index, formats[index]);
        expect(opened.has_value(), "supported FPK entry opens through a bounded span parser");
        if (!opened)
        {
            continue;
        }
        expect(opened->entryIndex == index && opened->format == formats[index],
               "opened FPK entry preserves selection and explicit format");
        const bool hasExpectedType =
            (index == 0 && std::holds_alternative<DdsDocument>(opened->data)) ||
            (index == 1 && std::holds_alternative<GfxDocument>(opened->data)) ||
            (index == 2 && std::holds_alternative<NifDocument>(opened->data)) ||
            (index == 3 && std::holds_alternative<Mp3Document>(opened->data)) ||
            (index == 4 && std::holds_alternative<MapDocument>(opened->data));
        expect(hasExpectedType, "FPK entry router returns the explicitly selected document type");
    }
    expect(document->bytes == archiveBytes,
           "in-memory FPK entry routing leaves retained archive bytes unchanged");

    const auto invalidSelection =
        OpenFpkEntryDocument(*document, document->index.entries.size(), FpkEntryFormat::Dds);
    expect(!invalidSelection &&
               invalidSelection.error() == "The selected FPK entry does not exist.",
           "invalid in-memory FPK selection reports its exact failure");

    auto invalidRange                    = *document;
    invalidRange.index.entries[0].offset = std::numeric_limits<std::uint32_t>::max();
    invalidRange.index.entries[0].size   = 2;
    const auto rangeResult               = OpenFpkEntryDocument(invalidRange, 0, FpkEntryFormat::Dds);
    expect(!rangeResult &&
               rangeResult.error() == "The selected FPK entry range is invalid.",
           "in-memory FPK routing reports its revalidated entry range failure");

    const std::array malformedPayloads{
        std::vector{ std::byte{ 'B' }, std::byte{ 'A' }, std::byte{ 'D' } }
    };
    const auto malformedBytes   = makeFpkWithPayloads(malformedPayloads);
    const auto malformedArchive = ParseFpkDocument(malformedBytes);
    expect(malformedArchive.has_value(), "synthetic malformed FPK payload remains bounded");
    if (malformedArchive)
    {
        const std::array expectedErrors{
            std::string_view{ "DDS metadata is truncated." },
            std::string_view{ "The file does not have a valid GFX signature." },
            std::string_view{ "The NIF file is truncated." },
            std::string_view{ "The MP3 audio could not be decoded." },
            std::string_view{
                "The map record is truncated. The Xbox DLC profile requires 1088 bytes." },
        };
        for (std::size_t index = 0; index < formats.size(); ++index)
        {
            const auto malformed = OpenFpkEntryDocument(*malformedArchive, 0, formats[index]);
            expect(!malformed && malformed.error() == expectedErrors[index],
                   "embedded FPK parser preserves its exact malformed-content failure");
        }
        expect(malformedArchive->bytes == malformedBytes,
               "failed embedded parsers leave malformed FPK source bytes unchanged");
    }

    const auto invalidFormat = OpenFpkEntryDocument(
        *document, 0, static_cast<FpkEntryFormat>(formats.size()));
    expect(!invalidFormat &&
               invalidFormat.error() == "The selected FPK entry format is invalid.",
           "invalid embedded FPK format reports a bounded failure");
    expect(document->bytes == archiveBytes,
           "failed in-memory FPK entry routing leaves retained archive bytes unchanged");

    const auto path = std::filesystem::temp_directory_path() /
                      "rerevved-tools-fpk-document-test.fpk";
    removeTestFile(path);
    {
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(archiveBytes.data()),
                     static_cast<std::streamsize>(archiveBytes.size()));
    }
    const auto loaded = LoadFpkDocument(path);
    expect(loaded && loaded->bytes == archiveBytes &&
               loaded->index.entries.size() == payloads.size(),
           "FPK document loader retains validated bytes and index");
    removeTestFile(path);

    const auto expectLoadError = [&](std::span<const std::byte> bytes,
                                     std::string_view           expected,
                                     std::string_view           message)
    {
        removeTestFile(path);
        {
            std::ofstream output(path, std::ios::binary);
            output.write(reinterpret_cast<const char*>(bytes.data()),
                         static_cast<std::streamsize>(bytes.size()));
        }
        const auto invalid = LoadFpkDocument(path);
        expect(!invalid && invalid.error() == expected, message);
        removeTestFile(path);
    };

    expectLoadError(std::span(archiveBytes).first(13),
                    "The FPK archive table is truncated.",
                    "FPK loader reports a clean truncated-table failure");

    auto invalidSignature = archiveBytes;
    invalidSignature[4]   = std::byte{ 'X' };
    expectLoadError(invalidSignature,
                    "The file does not have a valid FPK signature.",
                    "FPK loader reports a clean invalid-signature failure");

    auto unsupportedVersion = archiveBytes;
    writeU32Le(unsupportedVersion, 0, 5);
    expectLoadError(unsupportedVersion,
                    "The FPK archive version is unsupported.",
                    "FPK loader reports a clean unsupported-version failure");

    auto overflowingRange = archiveBytes;
    writeU32Le(overflowingRange, 26, 32);
    writeU32Le(overflowingRange, 30, 0xFFFFFFF0U);
    expectLoadError(overflowingRange,
                    "An FPK entry byte range overflows.",
                    "FPK loader reports a clean entry-range-overflow failure");

    auto outOfRange = archiveBytes;
    writeU32Le(outOfRange, 30, static_cast<std::uint32_t>(archiveBytes.size() + 1));
    expectLoadError(outOfRange,
                    "An FPK entry points outside the archive payload region.",
                    "FPK loader reports a clean out-of-archive failure");
}

void removeTestFile(const std::filesystem::path& path)
{
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

std::vector<unsigned char> readTestFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return { std::istreambuf_iterator<char>{ input }, {} };
}

void testFpkExtraction()
{
    using rerevved::tools::ExtractFpkEntry;
    using rerevved::tools::FpkExtractionError;
    using rerevved::tools::ParseFpkIndex;

    const auto bytes = makeFpk();
    const auto index = ParseFpkIndex(bytes);
    expect(index.has_value(), "synthetic FPK for extraction parses");
    if (!index)
    {
        return;
    }

    const auto temp         = std::filesystem::temp_directory_path();
    const auto outputPath   = temp / "rerevved-tools-fpk-extraction-output.bin";
    const auto existingPath = temp / "rerevved-tools-fpk-extraction-existing.bin";
    const auto invalidPath  = temp / "rerevved-tools-fpk-extraction-invalid.bin";
    const auto blockerPath  = temp / "rerevved-tools-fpk-extraction-blocker";
    removeTestFile(outputPath);
    removeTestFile(existingPath);
    removeTestFile(invalidPath);
    removeTestFile(blockerPath);

    const auto sourceBefore = bytes;
    const auto extraction   = ExtractFpkEntry(bytes, *index, 1, outputPath);
    expect(extraction.has_value(), "selected FPK entry extraction succeeds");
    expect(readTestFile(outputPath) ==
               std::vector<unsigned char>{ 0x61, 0x62, 0x63 },
           "only the selected FPK payload bytes are written");
    expect(bytes == sourceBefore, "FPK extraction leaves source bytes unchanged");
    removeTestFile(outputPath);

    const auto invalidSelection =
        ExtractFpkEntry(bytes, *index, index->entries.size(), invalidPath);
    expect(!invalidSelection &&
               invalidSelection.error() == FpkExtractionError::InvalidSelection,
           "invalid FPK selection is rejected");
    expect(!std::filesystem::exists(invalidPath),
           "invalid FPK selection creates no destination");

    auto invalidIndex              = *index;
    invalidIndex.entries[0].offset = static_cast<std::uint32_t>(bytes.size());
    const auto invalidRange        = ExtractFpkEntry(bytes, invalidIndex, 0, invalidPath);
    expect(!invalidRange &&
               invalidRange.error() == FpkExtractionError::InvalidEntryRange,
           "invalid FPK entry range is revalidated");
    expect(!std::filesystem::exists(invalidPath),
           "invalid FPK range creates no destination");

    invalidIndex.entries[0].offset = 0xFFFFFFFEU;
    invalidIndex.entries[0].size   = 3;
    const auto invalidRangeOverflow =
        ExtractFpkEntry(bytes, invalidIndex, 0, invalidPath);
    expect(!invalidRangeOverflow &&
               invalidRangeOverflow.error() == FpkExtractionError::InvalidEntryRange,
           "overflowing FPK entry range is revalidated");
    expect(!std::filesystem::exists(invalidPath),
           "overflowing FPK range creates no destination");

    {
        std::ofstream existing(existingPath, std::ios::binary);
        existing.put(static_cast<char>(0x5A));
    }
    const auto existingDestination = ExtractFpkEntry(bytes, *index, 0, existingPath);
    expect(!existingDestination &&
               existingDestination.error() == FpkExtractionError::DestinationExists,
           "existing extraction destination is rejected");
    expect(readTestFile(existingPath) == std::vector<unsigned char>{ 0x5A },
           "existing extraction destination is not overwritten");
    removeTestFile(existingPath);

    {
        std::ofstream blocker(blockerPath, std::ios::binary);
        blocker.put(static_cast<char>(0x00));
    }
    const auto unavailableDestination =
        ExtractFpkEntry(bytes, *index, 0, blockerPath / "payload.bin");
    expect(!unavailableDestination &&
               unavailableDestination.error() == FpkExtractionError::OpenFailed,
           "unavailable extraction destination reports open failure");
    removeTestFile(blockerPath);
}

void writeWorkspaceFixture(const std::filesystem::path& path,
                           std::span<const std::byte>   bytes)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
}

std::vector<std::byte> makeWorkspaceValidDds()
{
    constexpr std::array pixels{
        std::byte{ 0x33 },
        std::byte{ 0x22 },
        std::byte{ 0x11 },
        std::byte{ 0xFF },
    };
    return makePreviewDds(1,
                          1,
                          0x00FF0000U,
                          0x0000FF00U,
                          0x000000FFU,
                          0xFF000000U,
                          pixels);
}

std::vector<std::byte> makeWorkspacePresentationFailureNif()
{
    auto       bytes  = makeSyntheticModelNif();
    const auto parsed = rerevved::tools::ParseNifDocument(bytes.bytes);
    if (!parsed || parsed->triShapeData.empty())
    {
        return bytes.bytes;
    }
    const auto& dataBlock = parsed->blocks[parsed->triShapeData.front().blockIndex];
    for (std::size_t vertex = 0; vertex < 3; ++vertex)
    {
        for (std::size_t component = 0; component < 3; ++component)
        {
            writeNifF32(bytes.bytes, dataBlock.offset + 9 + vertex * 12 + component * 4, 0.0F);
        }
    }
    return bytes.bytes;
}

void testArchiveWorkArea()
{
    using namespace rerevved::tools;
    const std::array payloads{ makeWorkspaceValidDds(), makeGfx(), makeWorkspaceValidDds(), makeSyntheticModelNif().bytes };
    const auto       bytes  = makeFpkWithPayloads(payloads);
    const auto       parsed = ParseFpkDocument(bytes);
    expect(parsed.has_value(), "work-area archive parses");
    if (!parsed)
    {
        return;
    }
    DocumentWorkspace workspace;
    for (std::uint64_t id = 1; id <= 2; ++id)
    {
        AssetDocument document;
        document.id  = id;
        document.fpk = *parsed;
        SelectInitialArchiveEntry(document.archive, *document.fpk);
        workspace.Documents().push_back(std::move(document));
    }
    expect(workspace.Select(1), "first archive becomes active");
    auto&      archive = *workspace.Active()->fpk;
    auto&      state   = workspace.Active()->archive;
    const auto filter  = [&](std::string_view query)
    {
        state.filter.fill('\0');
        std::copy(query.begin(), query.end(), state.filter.begin());
        UpdateArchiveFilter(archive, state);
    };
    filter("dDs");
    expect(state.visibleEntries == std::vector<std::size_t>{ 0, 2 } &&
               state.entryHints[0] == FpkEntryKind::DdsTexture && !state.selectedFormat &&
               !state.openedDocument && state.openAttempts == 0,
           "case-insensitive hints filter original identities without choosing or invoking a parser");
    OpenSelectedArchiveEntryInMemory(archive, state);
    expect(state.openAttempts == 0 && !state.openedDocument,
           "Choose parser cannot open even a supported hinted entry");
    state.selectedFormat = FpkEntryFormat::Dds;
    OpenSelectedArchiveEntryInMemory(archive, state);
    expect(state.openedDocument && state.openedDocument->entryIndex == 0,
           "explicit parser and open action produce the selected document");
    expect(SelectArchiveEntry(state, 4, 1) && state.selectedFormat == FpkEntryFormat::Dds && state.openedDocument,
           "reselecting the same entry preserves its explicit open");
    const auto next = FindArchiveNeighbor(state, true);
    expect(next == 2 && !FindArchiveNeighbor(state, false),
           "filtered neighbors skip unmatched original entries");
    if (next)
    {
        (void)SelectArchiveEntry(state, 4, *next + 1);
    }
    expect(state.selectedEntry == 2 && state.requestedEntry == 3 && !state.selectedFormat && !state.openedDocument &&
               FindArchiveNeighbor(state, false) == 0 && !FindArchiveNeighbor(state, true),
           "filtered Next keeps original identity and clears the old parser/document");
    state.selectedFormat = FpkEntryFormat::Dds;
    OpenSelectedArchiveEntryInMemory(archive, state);
    filter("no match");
    expect(state.visibleEntries.empty() && state.selectedEntry == 2 && state.openedDocument &&
               state.selectedFormat == FpkEntryFormat::Dds && !FindArchiveNeighbor(state, false) && !FindArchiveNeighbor(state, true),
           "empty filtering preserves a hidden selected document and disables neighbors");
    const auto attemptsAfterExplicitOpens = state.openAttempts;
    filter("DDS");
    expect(SelectArchiveEntry(state, 4, 2) && state.selectedEntry == 1 && state.appliedFilter == "DDS" &&
               FindArchiveNeighbor(state, false) == 0 && FindArchiveNeighbor(state, true) == 2 && !state.selectedFormat,
           "numeric jump addresses the original entry outside the filter and resets parser");
    expect(!SelectArchiveEntry(state, 4, UINT64_MAX) && state.selectedEntry == 1,
           "invalid filtered jump preserves original selection");
    expect(state.openAttempts == attemptsAfterExplicitOpens && !state.openedDocument && !state.selectedFormat,
           "metadata filtering and entry highlighting do not increment explicit open attempts");
    state.selectedFormat = FpkEntryFormat::Gfx;
    OpenSelectedArchiveEntryInMemory(archive, state);
    state.gfxInspector.selectedOriginalIndex = 0;
    state.gfxInspector.filter[0]             = 'a';
    state.extractionPath[0]                  = 'X';
    expect(workspace.Select(2), "switch to second archive");
    auto& second = workspace.Active()->archive;
    (void)SelectArchiveEntry(second, 4, 4);
    second.selectedFormat = FpkEntryFormat::Nif;
    OpenSelectedArchiveEntryInMemory(*workspace.Active()->fpk, second);
    second.nifPreview->navigation.zoom              = 2.0;
    second.nifPreview->navigation.projection        = NifProjectionMode::Yz;
    second.nifInspector.currentSection              = NifInspectorSection::Nodes;
    second.nifInspector.nodes.selectedOriginalIndex = 0;
    expect(workspace.Select(1) && state.selectedEntry == 1 && state.selectedFormat == FpkEntryFormat::Gfx &&
               state.openedDocument && state.openedDocument->entryIndex == 1 && state.appliedFilter == "DDS" && state.extractionPath[0] == 'X',
           "top-level switching preserves each archive's filter, identity, choice, extraction input and document");
    expect(workspace.Select(2) && second.nifPreview && second.nifPreview->navigation.zoom == 2.0,
           "switching back preserves embedded CPU preview controls");
    expect(state.gfxInspector.selectedOriginalIndex == 0 && state.gfxInspector.filter[0] == 'a' &&
               second.nifInspector.currentSection == NifInspectorSection::Nodes && second.nifInspector.nodes.selectedOriginalIndex == 0,
           "two archives preserve independent format navigation across reference switches");
    const auto                     attemptsBeforeBrowsing = second.openAttempts;
    const auto                     previewMesh            = second.nifPreview->navigation.selectedMesh;
    const std::vector<std::string> nodeLabels{ "Node 0", "Node 1" };
    const auto                     matchingNodes = FilterInspectorIndices(nodeLabels, "Node 1");
    expect(SelectInspectorIndex(second.nifInspector.nodes, 1, nodeLabels.size()) &&
               JumpInspectorIndex(second.nifInspector.nodes, "0", 0, nodeLabels.size()) &&
               InspectorSelectionHidden(second.nifInspector.nodes, matchingNodes) &&
               second.openAttempts == attemptsBeforeBrowsing && second.nifPreview->navigation.selectedMesh == previewMesh &&
               second.nifPreview->navigation.zoom == 2.0 && second.nifPreview->navigation.projection == NifProjectionMode::Yz,
           "filter, highlight and inspector jump do not parse or change Preview navigation");
    (void)SelectArchiveEntry(second, 4, 1);
    expect(!second.nifPreview && !second.openedDocument && !second.selectedFormat && state.openedDocument &&
               second.nifInspector.currentSection == NifInspectorSection::BlockTypes && !second.nifInspector.nodes.selectedOriginalIndex &&
               state.gfxInspector.selectedOriginalIndex == 0 && second.openAttempts == attemptsBeforeBrowsing,
           "entry change releases its embedded preview without affecting another archive");
    const auto& entry = archive.index.entries[2];
    filter(std::to_string(entry.offset));
    expect(std::ranges::find(state.visibleEntries, 2) != state.visibleEntries.end(), "decimal range metadata is searchable");
    const auto range = FormatFpkEntryRange(3, entry);
    filter(range.substr(range.find("0x"), 10));
    expect(std::ranges::find(state.visibleEntries, 2) != state.visibleEntries.end(), "hex range metadata is searchable");
    filter(FormatBytes(entry.unknown0));
    expect(std::ranges::find(state.visibleEntries, 2) != state.visibleEntries.end(), "raw unknown bytes remain searchable");
    filter("Entry: 3");
    expect(state.visibleEntries == std::vector<std::size_t>{ 2 }, "original entry number is searchable");
    filter("");
    expect(state.visibleEntries == std::vector<std::size_t>{ 0, 1, 2, 3 } && archive.bytes == bytes &&
               FormatFpkEntryRange(3, archive.index.entries[2]) == range,
           "clearing filter restores source order and preserves exact source bytes and ranges");
    auto invalid                  = archive;
    invalid.index.entries[0].size = UINT32_MAX;
    ArchiveExplorerState invalidState;
    (void)SelectArchiveEntry(invalidState, 4, 1);
    invalidState.selectedFormat = FpkEntryFormat::Dds;
    OpenSelectedArchiveEntryInMemory(invalid, invalidState);
    expect(!invalidState.openedDocument && invalidState.openError == "The selected FPK entry range is invalid.",
           "explicit archive opening still revalidates range and preserves exact error");
}

void testDocumentWorkspace()
{
    using namespace rerevved::tools;

    const auto      root = std::filesystem::temp_directory_path() /
                           ("rerevved-tools-document-workspace-s1-" +
                            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code ignored;
    if (std::filesystem::exists(root))
    {
        expect(false, "workspace test temporary root is unexpectedly occupied");
        return;
    }

    const auto validBytes    = makeWorkspaceValidDds();
    const auto validPath     = root / "valid.dds";
    const auto missingPath   = root / "missing.dds";
    const auto malformedPath = root / "malformed.dds";
    writeWorkspaceFixture(validPath, validBytes);
    writeWorkspaceFixture(malformedPath, {});

    DocumentWorkspace workspace;
    const auto        valid     = workspace.Open(validPath, OpenOrigin::TypedPath);
    const auto        missing   = workspace.Open(missingPath, OpenOrigin::Drop);
    const auto        malformed = workspace.Open(malformedPath, OpenOrigin::CommandLine);
    expect(valid.kind == OpenOutcomeKind::SupportedOpened && valid.documentId,
           "document workspace opens a valid typed path");
    expect(missing.kind == OpenOutcomeKind::IoRejected && !missing.documentId &&
               missing.status.label == "I/O rejected" && !missing.status.error.empty(),
           "document workspace rejects a missing dropped path");
    expect(malformed.kind == OpenOutcomeKind::InspectionOpened && malformed.documentId &&
               malformed.status.stage == DocumentStage::ParseFailure &&
               malformed.status.error == "DDS metadata is truncated.",
           "malformed recognized input remains an inspection-only document with its exact error");
    expect(workspace.Documents().size() == 2 && workspace.Activity().size() == 3,
           "mixed origins retain one activity result per input");

    const auto relativeRoot = std::filesystem::path(
        ".rerevved-tools-relative-s1-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto relativeFile = relativeRoot / "relative.dds";
    if (std::filesystem::exists(relativeRoot))
    {
        expect(false, "relative-path test root is unexpectedly occupied");
    }
    else
    {
        writeWorkspaceFixture(relativeFile, validBytes);
        std::error_code absoluteError;
        const auto      absoluteFile = std::filesystem::absolute(relativeFile, absoluteError);
        expect(!absoluteError && absoluteFile.is_absolute(),
               "relative-path test resolves an absolute source locator");
        if (!absoluteError)
        {
            DocumentWorkspace relativeWorkspace;
            const auto        relativeOpen  = relativeWorkspace.Open(relativeFile, OpenOrigin::Drop);
            const auto        relativeAgain = relativeWorkspace.Open(relativeFile, OpenOrigin::TypedPath);
            const auto        absoluteAlias = relativeWorkspace.Open(absoluteFile, OpenOrigin::CommandLine);
            expect(relativeOpen.documentId && relativeOpen.source == absoluteFile &&
                       relativeWorkspace.Documents().front().sourcePath == absoluteFile &&
                       relativeWorkspace.Documents().front().inspection.path == relativeFile,
                   "relative open stores an absolute source path while retaining its lexical key");
            expect(relativeAgain.kind == OpenOutcomeKind::ExistingSelected &&
                       relativeAgain.documentId == relativeOpen.documentId &&
                       absoluteAlias.kind == OpenOutcomeKind::SupportedOpened &&
                       absoluteAlias.documentId != relativeOpen.documentId &&
                       relativeWorkspace.Documents().size() == 2,
                   "relative reuse stays lexical and absolute spelling opens a separate document");
            expect(relativeWorkspace.Activity().size() == 3 &&
                       relativeWorkspace.Activity()[0].source == absoluteFile &&
                       relativeWorkspace.Activity()[1].source == absoluteFile &&
                       relativeWorkspace.Activity()[2].source == absoluteFile,
                   "relative-input Activity records the resolved absolute locator for each outcome");
            expect(SourceLabel(relativeWorkspace.Documents().front()).find("relative.dds - " + absoluteFile.string()) != std::string::npos,
                   "source labels present the resolved absolute identity");
        }
        std::filesystem::remove_all(relativeRoot, ignored);
    }

    const auto lexicalReuse = workspace.Open(validPath, OpenOrigin::Archive);
    expect(lexicalReuse.kind == OpenOutcomeKind::ExistingSelected &&
               lexicalReuse.documentId == valid.documentId && workspace.Documents().size() == 2,
           "existing lexical path is selected before inspecting the filesystem");
    std::filesystem::remove(validPath, ignored);
    const auto deletedReuse = workspace.Open(validPath, OpenOrigin::TypedPath);
    expect(deletedReuse.kind == OpenOutcomeKind::ExistingSelected &&
               deletedReuse.documentId == valid.documentId,
           "deleted existing lexical path still selects the retained document");

    const auto sameA = root / "left" / "same.dds";
    const auto sameB = root / "right" / "same.dds";
    writeWorkspaceFixture(sameA, validBytes);
    writeWorkspaceFixture(sameB, validBytes);
    const auto firstSame  = workspace.Open(sameA);
    const auto secondSame = workspace.Open(sameB);
    expect(firstSame.documentId && secondSame.documentId && firstSame.documentId != secondSame.documentId,
           "same-basename files receive independent document identities");
    if (firstSame.documentId && secondSame.documentId)
    {
        const auto firstDocument  = std::find_if(workspace.Documents().begin(),
                                                 workspace.Documents().end(),
                                                 [&](const auto& document)
                                                 {
                                                    return document.id == *firstSame.documentId;
                                                 });
        const auto secondDocument = std::find_if(workspace.Documents().begin(),
                                                 workspace.Documents().end(),
                                                 [&](const auto& document)
                                                 {
                                                     return document.id == *secondSame.documentId;
                                                 });
        expect(firstDocument != workspace.Documents().end() &&
                   secondDocument != workspace.Documents().end(),
               "same-basename documents remain addressable by stable id");
        if (firstDocument == workspace.Documents().end() ||
            secondDocument == workspace.Documents().end())
        {
            return;
        }
        expect(SourceLabel(*firstDocument) != SourceLabel(*secondDocument) &&
                   SourceLabel(*firstDocument).find("same.dds - ") != std::string::npos &&
                   SourceLabel(*firstDocument).find("#" + std::to_string(firstDocument->id) + " ") == 0,
               "source labels retain a stable id, basename, and full lexical path");
    }

    const auto ids = [&]
    {
        std::vector<std::uint64_t> result;
        for (const auto& document : workspace.Documents())
        {
            result.push_back(document.id);
        }
        return result;
    }();
    if (ids.size() >= 4)
    {
        workspace.Documents()[0].nifPreview.zoom = 2.0;
        expect(workspace.Select(ids[1]), "document selection accepts a stable id");
        expect(workspace.Close(ids[1]) && workspace.Active() && workspace.Active()->id == ids[2],
               "closing an active middle document selects its successor");
        expect(workspace.Select(ids[0]) && workspace.Close(ids[2]) && workspace.Active() &&
                   workspace.Active()->id == ids[0] && workspace.Documents()[0].nifPreview.zoom == 2.0,
               "closing a nonactive document preserves the active id and preview state");
        expect(workspace.Close(ids[0]) && workspace.Active() && workspace.Active()->id == ids[3],
               "closing an active first document selects the successor");
        expect(workspace.Close(ids[3]) && !workspace.Active(),
               "closing the only remaining document leaves an explicit empty selection");
    }

    const auto assertParse = [&](FileKind kind, std::string_view message)
    {
        const auto status = ClassifyDocumentFailure(kind, message);
        expect(status.stage == DocumentStage::ParseFailure && status.error == message,
               "canonical parser failure keeps its exact message");
    };
    assertParse(FileKind::GfxMovie, GfxDocumentErrorMessage(GfxDocumentError::InvalidSignature));
    assertParse(FileKind::Mp3Audio, Mp3DocumentErrorMessage(Mp3DocumentError::DecodeFailed));
    assertParse(FileKind::MapRecord, MapDocumentErrorMessage(MapDocumentError::Truncated));
    assertParse(FileKind::NifContainer, NifDocumentErrorMessage(NifDocumentError::InvalidLayout));
    const auto fpkFailure = ClassifyDocumentFailure(
        FileKind::FpkArchive, "The FPK archive table is truncated.");
    expect(fpkFailure.stage == DocumentStage::LoadFailure &&
               fpkFailure.error == "The FPK archive table is truncated.",
           "FPK parser text remains exact while its private stage mapping stays unavailable");
    const auto malformedFpkPath  = root / "malformed.fpk";
    auto       malformedFpkBytes = makeFpk();
    malformedFpkBytes.resize(13);
    writeWorkspaceFixture(malformedFpkPath, malformedFpkBytes);
    DocumentWorkspace fpkWorkspace;
    const auto        malformedFpk = fpkWorkspace.Open(malformedFpkPath);
    expect(malformedFpk.kind == OpenOutcomeKind::InspectionOpened && malformedFpk.documentId &&
               malformedFpk.status.stage == DocumentStage::LoadFailure &&
               malformedFpk.status.label == "Inspection only. Load failed (stage unavailable)" &&
               malformedFpk.status.error == "The FPK archive table is truncated.",
           "failed FPK loading retains an inspection document with an unclassified stage");

    const auto ddsCapability = ClassifyDocumentFailure(
        FileKind::DdsTexture,
        DdsDocumentErrorMessage(DdsDocumentError{ DdsPreviewError::UnsupportedFormat }));
    const auto ddsDecode = ClassifyDocumentFailure(
        FileKind::DdsTexture,
        DdsDocumentErrorMessage(DdsDocumentError{ DdsPreviewError::TruncatedPixelData }));
    const auto ddsParse = ClassifyDocumentFailure(
        FileKind::DdsTexture,
        DdsDocumentErrorMessage(DdsDocumentError{ DdsMetadataError::InvalidSignature }));
    expect(ddsCapability.stage == DocumentStage::CapabilityUnavailable &&
               ddsCapability.error == "This DDS encoding is not supported for preview.",
           "DDS unsupported encoding is a capability outcome");
    expect(ddsDecode.stage == DocumentStage::PreviewDecodeFailure &&
               ddsDecode.error == "The DDS top level pixel data is truncated.",
           "DDS pixel truncation is a CPU preview decode failure");
    expect(ddsParse.stage == DocumentStage::ParseFailure &&
               ddsParse.error == "The file does not have a valid DDS signature.",
           "DDS malformed metadata is a parse failure");
    expect(ClassifyDocumentFailure(FileKind::NifContainer,
                                   NifPreviewErrorMessage(NifPreviewError::DegenerateProjection))
                       .stage == DocumentStage::PresentationFailure &&
               ClassifyDocumentFailure(FileKind::NifContainer,
                                       NifModelErrorMessage(NifModelError::NoSupportedMeshes))
                       .stage == DocumentStage::AssemblyFailure,
           "NIF assembly and presentation diagnostics retain separate stages");
    expect(ClassifyDocumentFailure(FileKind::GfxMovie,
                                   "Could not open GFX file for reading.")
                   .stage == DocumentStage::LoadFailure,
           "unclassified loader errors remain load failures");

    DocumentStatus equalA{ DocumentStage::ParseFailure, "Inspection only. Parse failed", "x" };
    DocumentStatus equalB{ DocumentStage::ParseFailure, "Inspection only. Parse failed", "x" };
    expect(equalA == equalB && equalA != DocumentStatus{ equalA.stage, equalA.label, "y" },
           "document diagnostics support equality for duplicate suppression");

    ArchiveExplorerState embedded;
    embedded.selectedFormat       = FpkEntryFormat::Dds;
    embedded.openError            = "DDS metadata is truncated.";
    const auto embeddedParse      = DescribeEmbeddedDocument(embedded);
    embedded.openError            = "This DDS encoding is not supported for preview.";
    const auto embeddedCapability = DescribeEmbeddedDocument(embedded);
    embedded.openError            = "The DDS top level pixel data is truncated.";
    const auto embeddedDecode     = DescribeEmbeddedDocument(embedded);
    embedded.openError            = "Could not read DDS entry data.";
    const auto embeddedLoad       = DescribeEmbeddedDocument(embedded);
    expect(embeddedParse.stage == DocumentStage::ParseFailure &&
               embeddedCapability.stage == DocumentStage::CapabilityUnavailable &&
               embeddedDecode.stage == DocumentStage::PreviewDecodeFailure &&
               embeddedLoad.stage == DocumentStage::LoadFailure,
           "embedded DDS diagnostics map the same exact parser messages without mutation");

    embedded.openError.clear();
    embedded.openedDocument = FpkEntryDocument{ .data = GfxDocument{} };
    expect(DescribeEmbeddedDocument(embedded).label == "Metadata only",
           "embedded GFX retains its metadata-only label");
    embedded.openedDocument = FpkEntryDocument{ .data = NifDocument{} };
    embedded.nifPreview.emplace();
    embedded.nifPreview->assemblyError = NifModelError::NoSupportedMeshes;
    expect(DescribeEmbeddedDocument(embedded).stage == DocumentStage::AssemblyFailure,
           "embedded NIF assembly failures retain the Inspector document");
    embedded.nifPreview->assemblyError.reset();
    embedded.presentationError = NifPreviewErrorMessage(NifPreviewError::UnavailableRegion);
    expect(DescribeEmbeddedDocument(embedded).stage == DocumentStage::PresentationFailure &&
               DescribeEmbeddedDocument(embedded).error == embedded.presentationError,
           "embedded presentation failures retain the exact presentation message");

    const auto gfxPath     = root / "metadata.gfx";
    const auto bikPath     = root / "unsupported.bik";
    const auto unknownPath = root / "unknown.bin";
    writeWorkspaceFixture(gfxPath, makeGfx());
    writeWorkspaceFixture(bikPath, {});
    writeWorkspaceFixture(unknownPath, {});
    DocumentWorkspace metadataWorkspace;
    const auto        gfx     = metadataWorkspace.Open(gfxPath);
    const auto        bik     = metadataWorkspace.Open(bikPath);
    const auto        unknown = metadataWorkspace.Open(unknownPath);
    expect(gfx.status.stage == DocumentStage::Ready && gfx.status.label == "Metadata only" &&
               bik.status.label == "Inspection only. BIK preview unsupported" &&
               unknown.status.label == "Inspection only. No preview capability",
           "GFX, BIK, and unknown formats expose explicit metadata/capability labels");

    const auto dx10Path            = root / "dx10.dds";
    const auto truncatedPixelsPath = root / "truncated-pixels.dds";
    const auto malformedHeaderPath = root / "malformed-header.dds";
    writeWorkspaceFixture(dx10Path, makeDds(true));
    writeWorkspaceFixture(truncatedPixelsPath,
                          makePreviewDds(1,
                                         1,
                                         0x00FF0000U,
                                         0x0000FF00U,
                                         0x000000FFU,
                                         0xFF000000U,
                                         {}));
    auto malformedHeader = makeDds(false);
    malformedHeader[0]   = std::byte{ 0 };
    writeWorkspaceFixture(malformedHeaderPath, malformedHeader);
    DocumentWorkspace ddsWorkspace;
    const auto        dx10                   = ddsWorkspace.Open(dx10Path);
    const auto        truncatedPixels        = ddsWorkspace.Open(truncatedPixelsPath);
    const auto        malformedHeaderOutcome = ddsWorkspace.Open(malformedHeaderPath);
    expect(dx10.status.stage == DocumentStage::CapabilityUnavailable &&
               truncatedPixels.status.stage == DocumentStage::PreviewDecodeFailure &&
               malformedHeaderOutcome.status.stage == DocumentStage::ParseFailure,
           "direct DX10, truncated-pixel, and malformed-header DDS outcomes stay distinct");

    const auto nifFixture           = makeSyntheticModelNif();
    auto       assemblyFailureBytes = nifFixture.bytes;
    writeU32Be(assemblyFailureBytes, nifFixture.rootReferenceOffset, UINT32_MAX);
    const auto nifPath = root / "assembly-failure.nif";
    writeWorkspaceFixture(nifPath, assemblyFailureBytes);
    DocumentWorkspace nifWorkspace;
    const auto        nif = nifWorkspace.Open(nifPath);
    expect(nif.status.stage == DocumentStage::AssemblyFailure && nif.documentId &&
               nifWorkspace.Active() && nifWorkspace.Active()->nif,
           "direct NIF assembly failure retains parsed Inspector data");

    const auto presentationNif      = makeWorkspacePresentationFailureNif();
    const auto presentationDocument = ParseNifDocument(presentationNif);
    const auto presentationModel    = presentationDocument
                                          ? AssembleNifModel(*presentationDocument)
                                          : std::expected<NifModel, NifModelError>{
                                                std::unexpect,
                                                NifModelError::NoSupportedMeshes
                                            };
    bool       presentationFailed   = false;
    if (presentationDocument && presentationModel)
    {
        std::vector<NifPreviewMeshView> views;
        for (const auto& mesh : presentationModel->meshes)
        {
            const auto& geometry = presentationDocument->triShapeData[mesh.triShapeDataIndex];
            views.push_back({ geometry.vertexPositions, geometry.triangles, mesh.worldTransform });
        }
        presentationFailed = !CalculateNifWireframeLayout(
            views, 320.0F, 240.0F, NifProjectionMode::Automatic);
    }
    expect(presentationDocument && presentationModel && presentationFailed,
           "presentation-failure NIF remains parser/assembly-valid but cannot fit a wireframe");

    const auto validInspectorNifPath = root / "valid-inspector.nif";
    writeWorkspaceFixture(validInspectorNifPath, nifFixture.bytes);
    const auto validInspectorNif = nifWorkspace.Open(validInspectorNifPath);
    expect(validInspectorNif.documentId && nif.documentId &&
               validInspectorNif.kind == OpenOutcomeKind::SupportedOpened,
           "document workspace opens a second retained NIF owner");
    if (nif.documentId && validInspectorNif.documentId)
    {
        expect(nifWorkspace.Select(*nif.documentId) && nifWorkspace.Active(),
               "document workspace selects the first NIF owner");
        if (nifWorkspace.Active())
        {
            nifWorkspace.Active()->nifInspector.currentSection =
                NifInspectorSection::SourceTextures;
            nifWorkspace.Active()->nifInspector.sourceTextures.selectedOriginalIndex = 7;
            nifWorkspace.Active()->gfxInspector.selectedOriginalIndex                = 2;
        }
        expect(nifWorkspace.Select(*validInspectorNif.documentId) && nifWorkspace.Active(),
               "document workspace switches to the second NIF owner");
        if (nifWorkspace.Active())
        {
            nifWorkspace.Active()->nifInspector.currentSection               = NifInspectorSection::Blocks;
            nifWorkspace.Active()->nifInspector.blocks.selectedOriginalIndex = 1;
        }
        expect(nifWorkspace.Select(*nif.documentId) && nifWorkspace.Active() &&
                   nifWorkspace.Active()->nifInspector.currentSection ==
                       NifInspectorSection::SourceTextures &&
                   nifWorkspace.Active()->nifInspector.sourceTextures.selectedOriginalIndex == 7 &&
                   nifWorkspace.Active()->gfxInspector.selectedOriginalIndex == 2,
               "switching back preserves the first direct NIF Inspector state");
        expect(nifWorkspace.Select(*validInspectorNif.documentId) && nifWorkspace.Active() &&
                   nifWorkspace.Active()->nifInspector.currentSection == NifInspectorSection::Blocks &&
                   nifWorkspace.Active()->nifInspector.blocks.selectedOriginalIndex == 1,
               "switching back preserves the second direct NIF Inspector state independently");
        const auto existingNif = nifWorkspace.Open(nifPath);
        expect(existingNif.kind == OpenOutcomeKind::ExistingSelected &&
                   existingNif.documentId == nif.documentId && nifWorkspace.Active() &&
                   nifWorkspace.Active()->nifInspector.currentSection ==
                       NifInspectorSection::SourceTextures,
               "reopening the same direct NIF selects its existing Inspector state");
        expect(nifWorkspace.Close(*nif.documentId),
               "closing a direct NIF owner succeeds");
        const auto reopenedNif = nifWorkspace.Open(nifPath);
        expect(reopenedNif.documentId && reopenedNif.documentId != nif.documentId &&
                   nifWorkspace.Active() &&
                   nifWorkspace.Active()->nifInspector.currentSection ==
                       NifInspectorSection::BlockTypes &&
                   !nifWorkspace.Active()->nifInspector.sourceTextures.selectedOriginalIndex,
               "reopening after close receives an independent reset Inspector state");
    }

    const std::array     inspectorPayloads{ nifFixture.bytes, makeGfx(), nifFixture.bytes };
    const auto           inspectorArchiveBytes = makeFpkWithPayloads(inspectorPayloads);
    const auto           inspectorArchive      = ParseFpkDocument(inspectorArchiveBytes);
    ArchiveExplorerState inspectorArchiveState;
    if (inspectorArchive)
    {
        expect(SelectArchiveEntry(inspectorArchiveState,
                                  inspectorArchive->index.entries.size(),
                                  1),
               "archive Inspector selects its first embedded entry");
        inspectorArchiveState.selectedFormat = FpkEntryFormat::Nif;
        OpenSelectedArchiveEntryInMemory(*inspectorArchive, inspectorArchiveState);
        inspectorArchiveState.nifInspector.currentSection              = NifInspectorSection::Nodes;
        inspectorArchiveState.nifInspector.nodes.selectedOriginalIndex = 3;
        expect(SelectArchiveEntry(inspectorArchiveState,
                                  inspectorArchive->index.entries.size(),
                                  1) &&
                   inspectorArchiveState.nifInspector.currentSection == NifInspectorSection::Nodes &&
                   inspectorArchiveState.nifInspector.nodes.selectedOriginalIndex == 3,
               "same embedded entry preserves its Inspector state");
        expect(SelectArchiveEntry(inspectorArchiveState,
                                  inspectorArchive->index.entries.size(),
                                  3) &&
                   inspectorArchiveState.nifInspector.currentSection ==
                       NifInspectorSection::BlockTypes &&
                   !inspectorArchiveState.nifInspector.nodes.selectedOriginalIndex,
               "changing embedded entries resets Inspector state");
        inspectorArchiveState.selectedFormat = FpkEntryFormat::Nif;
        OpenSelectedArchiveEntryInMemory(*inspectorArchive, inspectorArchiveState);
        inspectorArchiveState.nifInspector.currentSection                     = NifInspectorSection::TriangleData;
        inspectorArchiveState.nifInspector.triangleData.selectedOriginalIndex = 1;
        CloseOpenedArchiveEntry(inspectorArchiveState);
        expect(!inspectorArchiveState.nifInspector.triangleData.selectedOriginalIndex &&
                   !inspectorArchiveState.openedDocument,
               "closing an embedded entry clears its Inspector state");
        (void)SelectArchiveEntry(inspectorArchiveState,
                                 inspectorArchive->index.entries.size(),
                                 3);
        inspectorArchiveState.selectedFormat = FpkEntryFormat::Nif;
        OpenSelectedArchiveEntryInMemory(*inspectorArchive, inspectorArchiveState);
        expect(inspectorArchiveState.nifInspector.currentSection ==
                       NifInspectorSection::BlockTypes &&
                   !inspectorArchiveState.nifInspector.triangleData.selectedOriginalIndex,
               "reopening an embedded entry starts with independent Inspector state");
    }

    for (std::size_t index = 0; index < 300; ++index)
    {
        workspace.RecordActivity(OpenOutcome{ .origin     = OpenOrigin::Presentation,
                                              .kind       = OpenOutcomeKind::PreviewUnavailable,
                                              .source     = std::to_string(index),
                                              .documentId = std::nullopt,
                                              .status     = DocumentStatus{} });
    }
    expect(workspace.Activity().size() == 256 &&
               workspace.Activity().back().source == std::filesystem::path("299"),
           "workspace Activity keeps only the latest 256 bounded outcomes");

    std::filesystem::remove_all(root, ignored);
}

void writeWorkspaceFixtures(const std::filesystem::path& root)
{
    if (!root.is_absolute() || std::filesystem::exists(root))
    {
        std::cerr << "fixture directory must be absolute and absent\n";
        ++failures;
        return;
    }
    const auto validDds      = makeWorkspaceValidDds();
    auto       malformedDds  = makeDds(false);
    malformedDds[0]          = std::byte{ 0 };
    const auto truncatedDds  = makePreviewDds(1,
                                              1,
                                              0x00FF0000U,
                                              0x0000FF00U,
                                              0x000000FFU,
                                              0xFF000000U,
                                              {});
    const auto validNif      = makeSyntheticModelNif().bytes;
    const auto navigationNif = makeSyntheticSourceNavigationNif();
    const auto navigationGfx = makeNavigationGfx();
    auto       malformedNif  = validNif;
    malformedNif.resize(8);
    std::vector<std::byte>       validMap(rerevved::tools::kMapEnvelopeSize, std::byte{ 0 });
    const auto                   mp3Span = std::as_bytes(std::span(rerevved::tools::test::kSyntheticMp3.data(),
                                                                   rerevved::tools::test::kSyntheticMp3.size()));
    const std::vector<std::byte> validMp3(mp3Span.begin(), mp3Span.end());
    auto                         malformedFpk = makeFpk();
    malformedFpk.resize(13);
    const std::vector<std::byte> malformedGfx;
    const std::vector<std::byte> malformedMp3{ std::byte{ 'n' }, std::byte{ 'o' }, std::byte{ 't' } };
    const std::vector<std::byte> malformedMap{ std::byte{ 0 } };
    const std::array             fpkPayloads{
        validDds,
        makeGfx(),
        validMp3,
        validMap,
        validNif,
        makeDds(true),
        truncatedDds,
        malformedDds,
        malformedGfx,
        malformedMp3,
        malformedMap,
        malformedNif,
    };
    const auto fpk = makeFpkWithPayloads(fpkPayloads);
    writeWorkspaceFixture(root / "valid.dds", validDds);
    writeWorkspaceFixture(root / "malformed.dds", malformedDds);
    writeWorkspaceFixture(root / "dx10.dds", makeDds(true));
    writeWorkspaceFixture(root / "truncated-pixels.dds", truncatedDds);
    writeWorkspaceFixture(root / "valid.gfx", makeGfx());
    writeWorkspaceFixture(root / "multi-record-navigation.gfx", navigationGfx);
    writeWorkspaceFixture(root / "malformed.gfx", malformedGfx);
    writeWorkspaceFixture(root / "valid.mp3", validMp3);
    writeWorkspaceFixture(root / "malformed.mp3", malformedMp3);
    writeWorkspaceFixture(root / "valid.map", validMap);
    writeWorkspaceFixture(root / "malformed.map", malformedMap);
    writeWorkspaceFixture(root / "valid.nif", validNif);
    writeWorkspaceFixture(root / "multi-record-source-textures.nif", navigationNif);
    writeWorkspaceFixture(root / "multi-record.nif", makeSyntheticMaterialNif().bytes);
    writeWorkspaceFixture(root / "texture-properties.nif", makeSyntheticTextureNif().bytes);
    writeWorkspaceFixture(root / "malformed.nif", malformedNif);
    auto assemblyFixture = makeSyntheticModelNif();
    writeU32Be(assemblyFixture.bytes, assemblyFixture.rootReferenceOffset, UINT32_MAX);
    writeWorkspaceFixture(root / "assembly-failure.nif", assemblyFixture.bytes);
    writeWorkspaceFixture(root / "presentation-failure.nif",
                          makeWorkspacePresentationFailureNif());
    writeWorkspaceFixture(root / "unsupported.bik", {});
    writeWorkspaceFixture(root / "unknown.bin", {});
    writeWorkspaceFixture(root / "malformed.fpk", malformedFpk);
    writeWorkspaceFixture(root / "multi-entry.fpk", fpk);
    const std::array navigationPayloads{ navigationNif, navigationGfx };
    writeWorkspaceFixture(root / "multi-record-navigation.fpk", makeFpkWithPayloads(navigationPayloads));
    const std::array materialPayloads{ makeSyntheticMaterialNif().bytes, makeGfx() };
    writeWorkspaceFixture(root / "multi-record.fpk", makeFpkWithPayloads(materialPayloads));
    const std::array texturePayloads{ makeSyntheticTextureNif().bytes };
    writeWorkspaceFixture(root / "texture-properties.fpk", makeFpkWithPayloads(texturePayloads));
    writeWorkspaceFixture(root / "left" / "same.dds", validDds);
    writeWorkspaceFixture(root / "right" / "same.dds", validDds);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc == 3 && std::string_view(argv[1]) == "--write-workspace-fixtures")
    {
        writeWorkspaceFixtures(std::filesystem::path(argv[2]));
        return failures == 0 ? 0 : 1;
    }
    testFileKinds();
    testCreatorActivityKinds();
    testDocumentErrorMessages();
    testInspectorNavigation();
    testRetainedAssetInspectorPresentation();
    testArchiveNavigation();
    testExtractionModalLayout();
    testArchiveActionRowLayout();
    testArchiveMetadataLayout();
    testFpkEntryRangeFormatting();
    testInspection();
    testDdsMetadata();
    testDdsDocument();
    testGfxDocument();
    testMapDocument();
    testMp3Document();
    testNifDocument();
    testNifGeometryInventory();
    testSyntheticModelFixture();
    testNifMaterialPropertyInventory();
    testNifTextureSourceInventory();
    testNifMaterialInspectorPresentation();
    testNifTextureSourceInspectorPresentation();
    testNifSourceNavigationFixture();
    testNifModelAssembly();
    testNifWireframePreview();
    testNifWireframeNavigation();
    testCloseOpenedArchiveEntry();
    testEmbeddedNifWireframePreview();
    testInitialArchiveSelection();
    testFpkIndex();
    testFpkEntryIdentification();
    testFpkDocument();
    testFpkExtraction();
    testDocumentWorkspace();
    testArchiveWorkArea();
    if (failures != 0)
    {
        std::cerr << failures << " test(s) failed.\n";
    }
    return failures == 0 ? 0 : 1;
}
