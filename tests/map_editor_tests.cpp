#include "map/creator_workspace.h"
#include "map/map_edit_model.h"
#include "map/map_editor.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <string_view>
#include <utility>

namespace
{

using namespace rerevved::tools;

int failures = 0;

void expect(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::array<std::byte, kMapEnvelopeSize> syntheticEnvelope()
{
    std::array<std::byte, kMapEnvelopeSize> bytes{};
    for (std::size_t offset = 0; offset < bytes.size(); ++offset)
    {
        bytes[offset] = static_cast<std::byte>((offset * 37 + 0x30) & 0xFF);
    }
    return bytes;
}

bool sameDocument(const MapDocument& left, const MapDocument& right)
{
    return left.core == right.core && left.footer == right.footer;
}

void testStartAndExactImport()
{
    MapEditModel model;
    expect(model.Document() == nullptr && model.Source() == nullptr, "new model has no draft or source");
    expect(model.StartSynthetic().has_value(), "start synthetic fixed-profile draft");
    expect(model.Synthetic(), "synthetic provenance is retained");
    expect(std::ranges::all_of(model.Document()->core, [](std::byte value)
                               {
                                   return value == std::byte{ 0 };
                               }),
           "seed core is explicitly all zero");
    expect(std::ranges::all_of(model.Document()->footer, [](std::byte value)
                               {
                                   return value == std::byte{ 0xFF };
                               }),
           "seed footer is explicitly all FF");
    auto duplicate = model.StartSynthetic();
    expect(!duplicate && duplicate.error() == MapEditError::DraftExists, "new draft cannot replace existing draft");
    model.Reset();
    expect(!model.Document() && !model.CanUndo() && !model.CanRedo(), "discard resets bytes and history");

    auto       bytes    = syntheticEnvelope();
    const auto original = bytes;
    expect(model.ImportBytes(bytes).has_value(), "import exact 1088-byte record through existing parser");
    expect(!model.Synthetic(), "import provenance is distinct from synthetic seed");
    expect(std::ranges::equal(model.Document()->core, std::span(bytes).first(kMapCoreSize)), "import preserves all core bytes");
    expect(std::ranges::equal(model.Document()->footer, std::span(bytes).last(kMapFooterSize)), "import preserves every non-FF footer byte");
    bytes[0] = std::byte{ 0 };
    expect(model.Document()->core[0] == original[0] && model.Source()->core[0] == original[0], "draft and immutable source own input bytes");
    const auto snapshot    = *model.Document();
    const auto replacement = model.ImportBytes(original);
    expect(!replacement && replacement.error() == MapEditErrorMessage(MapEditError::DraftExists), "import replacement requires explicit discard");
    expect(sameDocument(*model.Document(), snapshot), "refused import retains exact draft");

    model.Reset();
    const auto shortImport = model.ImportBytes(std::span(original).first(kMapCoreSize));
    expect(!shortImport && shortImport.error() == MapDocumentErrorMessage(MapDocumentError::Truncated), "bare core reports canonical truncated error");
    std::array<std::byte, kMapEnvelopeSize + 1> oversized{};
    const auto                                  longImport = model.ImportBytes(oversized);
    expect(!longImport && longImport.error() == MapDocumentErrorMessage(MapDocumentError::UnsupportedLength), "extra byte reports canonical unsupported length");
    expect(!model.Document(), "failed imports never start a draft");
}

void testBrushMasksPreserveRawBitsAndFooter()
{
    const auto bytes = syntheticEnvelope();
    for (const auto settings : { MapBrushSettings{ MapBrush::TerrainCode, 0x05, true },
                                 MapBrushSettings{ MapBrush::StartMarker, 0, true },
                                 MapBrushSettings{ MapBrush::StartMarker, 0, false } })
    {
        MapEditModel model;
        expect(model.ImportBytes(bytes).has_value(), "brush fixture imports");
        const auto before = *model.Document();
        const auto mask   = MapBrushMask(settings.tool);
        expect(model.BeginStroke(settings).has_value(), "begin masked brush stroke");
        for (std::size_t offset = 0; offset < kMapCoreSize; ++offset)
        {
            expect(model.Paint(offset).has_value(), "every core cell is paintable");
            const auto oldValue = std::to_integer<unsigned int>(before.core[offset]);
            const auto newValue = std::to_integer<unsigned int>(model.Document()->core[offset]);
            expect(((oldValue ^ newValue) & 0x10U) == 0, "all brushes preserve raw 0x10 for every byte combination");
            expect(((oldValue ^ newValue) & static_cast<unsigned int>(~mask & 0xFF)) == 0, "brush changes only its named mask");
            const auto expectedBits = settings.tool == MapBrush::TerrainCode ? settings.terrainCode : (settings.set ? mask : 0U);
            expect((newValue & mask) == expectedBits, "brush applies exact numeric value or explicit Set/Clear");
        }
        expect(model.EndStroke(), "changed drag commits one history step");
        const auto painted = *model.Document();
        expect(painted.footer == before.footer, "non-FF footer remains exact after every brush");
        expect(sameDocument(*model.Source(), before), "brush never mutates immutable imported source");
        expect(model.Undo() && sameDocument(*model.Document(), before), "one undo restores every affected byte and footer");
        expect(!model.CanUndo(), "one drag creates exactly one undo step");
        expect(model.Redo() && sameDocument(*model.Document(), painted), "redo restores exact painted bytes and footer");
    }
}

void testReplaceTiles()
{
    MapEditModel model;
    expect(!model.ReplaceTiles({}) && model.ReplaceTiles({}).error() == MapEditError::NoDraft, "replacing needs a draft");
    expect(model.StartSynthetic().has_value(), "start replace fixture");
    const auto initial = *model.Document();
    auto       tiles   = initial.core;
    tiles[5]           = std::byte{ 0x03 };
    tiles[900]         = std::byte{ 0x24 };
    expect(model.ReplaceTiles(tiles).value_or(false), "replacing changed tiles reports a change");
    expect(model.Document()->core == tiles, "every tile is replaced");
    expect(!model.ReplaceTiles(tiles).value_or(true), "replacing with the same tiles adds no step");
    expect(model.Undo() && sameDocument(*model.Document(), initial), "one undo restores every replaced tile");
    expect(!model.CanUndo() && model.Redo() && model.Document()->core == tiles, "a replacement is one undo step");
    expect(model.BeginStroke({ MapBrush::StartMarker }).has_value(), "begin a stroke");
    expect(!model.ReplaceTiles(initial.core) && model.ReplaceTiles(initial.core).error() == MapEditError::StrokeActive, "replacing waits for the stroke");
}

void testStrokeTransactionsAndHistory()
{
    MapEditModel model;
    const auto   missing = model.BeginStroke({ MapBrush::StartMarker });
    expect(!missing && missing.error() == MapEditError::NoDraft, "brush requires draft");
    expect(model.StartSynthetic().has_value(), "start history fixture");
    const auto initial = *model.Document();
    expect(model.BeginStroke({ MapBrush::StartMarker, 0, true }).has_value(), "begin explicit Set stroke");
    expect(model.Paint(0).value_or(false), "first Set changes byte");
    expect(!model.Paint(0).value_or(true), "revisiting cell does not toggle flag");
    expect(model.Paint(1023).value_or(false), "same stroke reaches last cell");
    expect(!model.CanUndo() && !model.Undo(), "incomplete stroke cannot undo");
    expect(model.CancelStroke() && sameDocument(*model.Document(), initial), "Escape cancellation restores full before bytes");
    expect(!model.CanUndo(), "cancelled stroke adds no history");
    expect(model.BeginStroke({ MapBrush::StartMarker, 0, true }).has_value(), "begin history stroke");
    expect(model.Paint(1023).value_or(false) && model.EndStroke(), "commit first history step");
    expect(model.BeginStroke({ MapBrush::StartMarker, 0, true }).has_value() && model.CanUndo() && !model.Undo(), "a stroke refuses undo but keeps the prompt steady");
    static_cast<void>(model.CancelStroke());
    expect(model.Undo(), "undo first step");
    expect(model.BeginStroke({ MapBrush::SelectTile }).has_value(), "begin Select tool stroke");
    expect(!model.Paint(31).value_or(true) && !model.EndStroke(), "Select tool changes only tile selection");
    expect(model.SelectedTile() == 31 && model.CanRedo(), "selection and no-op strokes preserve redo history");
    expect(model.BeginStroke({ MapBrush::TerrainCode, 7 }).has_value(), "begin divergent edit");
    expect(model.Paint(31).value_or(false) && model.EndStroke(), "commit divergent edit");
    expect(!model.CanRedo(), "changed stroke after undo clears abandoned redo");
    expect(model.Undo() && sameDocument(*model.Document(), initial), "divergent undo restores exact original envelope");

    model.Reset();
    expect(model.StartSynthetic().has_value(), "start bounded history fixture");
    for (std::size_t stroke = 0; stroke < kMapUndoLimit + 5; ++stroke)
    {
        expect(model.BeginStroke({ MapBrush::StartMarker, 0, stroke % 2 == 0 }).has_value(), "begin bounded-history stroke");
        expect(model.Paint(0).value_or(false) && model.EndStroke(), "commit bounded-history stroke");
    }
    std::size_t undoCount = 0;
    while (model.Undo())
    {
        ++undoCount;
    }
    expect(undoCount == kMapUndoLimit, "history retains a bounded 128 strokes");
}

void testInvalidEditsKeepBytesAndSelection()
{
    MapEditModel model;
    expect(model.StartSynthetic().has_value(), "start invalid edit fixture");
    expect(model.SelectTile(1023).has_value(), "select corner before invalid jump");
    const auto invalidTile = model.SelectTile(1024);
    expect(!invalidTile && invalidTile.error() == MapEditError::InvalidTile && model.SelectedTile() == 1023, "invalid jump preserves prior selection");
    const auto before      = *model.Document();
    const auto invalidCode = model.BeginStroke({ MapBrush::TerrainCode, 8 });
    expect(!invalidCode && invalidCode.error() == MapEditError::InvalidTerrainCode, "codes without game terrain are refused");
    const auto invalidBrush = model.BeginStroke({ static_cast<MapBrush>(99) });
    expect(!invalidBrush && invalidBrush.error() == MapEditError::InvalidBrush, "unknown brush is refused");
    const auto noStroke = model.Paint(0);
    expect(!noStroke && noStroke.error() == MapEditError::NoStroke && model.SelectedTile() == 1023, "paint without stroke retains selection");
    expect(model.BeginStroke({ MapBrush::StartMarker }).has_value(), "begin valid stroke after errors");
    const auto nestedStroke = model.BeginStroke({ MapBrush::StartMarker });
    expect(!nestedStroke && nestedStroke.error() == MapEditError::StrokeActive, "nested stroke is refused");
    const auto outside = model.Paint(1024);
    expect(!outside && outside.error() == MapEditError::InvalidTile, "brush cannot address footer");
    expect(!model.EndStroke() && sameDocument(*model.Document(), before), "invalid operations retain exact envelope and history");
}

void testCancelInterruptedStrokeRestoresImportedBytes()
{
    MapEditModel model;
    expect(model.ImportBytes(syntheticEnvelope()).has_value(), "import interrupted-stroke fixture");
    const auto source = *model.Source();
    const bool marked = (std::to_integer<unsigned int>(source.core[0]) & 0x20U) != 0;
    expect(model.BeginStroke({ MapBrush::StartMarker, 0, !marked }).has_value(), "begin completed stroke before interruption");
    expect(model.Paint(0).value_or(false) && model.EndStroke(), "retain a committed edit before interruption");
    const auto committed = *model.Document();
    expect(model.BeginStroke({ MapBrush::TerrainCode, 7 }).has_value(), "begin interrupted terrain stroke");
    for (std::size_t offset = 0; offset < kMapCoreSize; ++offset)
    {
        expect(model.Paint(offset).has_value(), "interrupted stroke paints every imported tile");
    }
    const auto paintedRevision = model.Revision();
    expect(model.CancelStroke(), "hidden editor or journey switch can cancel open stroke");
    expect(!model.StrokeActive(), "cancel releases stroke capture immediately");
    expect(sameDocument(*model.Document(), committed), "cancel restores every before byte including non-FF footer");
    expect(sameDocument(*model.Source(), source), "cancel preserves immutable imported bytes");
    expect(model.Revision() > paintedRevision, "cancel invalidates results for temporary painted bytes");
    expect(!model.EndStroke(), "later mouse release cannot commit cancelled stroke");
    expect(model.Undo() && sameDocument(*model.Document(), source), "cancel preserves preceding undo transaction");
    expect(model.Redo() && sameDocument(*model.Document(), committed), "cancel preserves preceding redo transaction");
}

void testNoOpStrokeCancelPreservesRevision()
{
    CreatorWorkspace workspace;
    MapEditorState   state;
    CancelMapEditorStroke(workspace, state);
    expect(!workspace.Map().edited && workspace.Map().revision == 0, "cancelling without a stroke leaves an untouched workspace clean");
    expect(workspace.StartMapDraft("No-op cancellation").has_value() && state.model.StartSynthetic().has_value(), "start no-op cancellation fixture");
    expect(state.model.BeginStroke({ MapBrush::StartMarker }).has_value(), "begin preceding history stroke");
    expect(state.model.Paint(0).value_or(false) && state.model.EndStroke() && state.model.Undo(), "retain redo history before cancellation");
    const auto before        = *state.model.Document();
    const auto modelRevision = state.model.Revision();
    const auto draftRevision = workspace.Map().revision;
    const auto edited        = workspace.Map().edited;
    for (const auto brush : { MapBrushSettings{ MapBrush::TerrainCode, 0 }, MapBrushSettings{ MapBrush::SelectTile } })
    {
        expect(state.model.BeginStroke(brush).has_value(), "begin no-op brush stroke");
        expect(!state.model.Paint(31).value_or(true), "no-op brush changes no byte");
        CancelMapEditorStroke(workspace, state);
        expect(!state.model.StrokeActive() && state.model.Revision() == modelRevision, "no-op cancel closes stroke without revising model");
        expect(workspace.Map().revision == draftRevision && workspace.Map().edited == edited, "no-op cancel preserves draft revision and edited state");
        expect(sameDocument(*state.model.Document(), before) && !state.model.CanUndo() && state.model.CanRedo(), "no-op cancel preserves exact bytes and redo history");
    }
    expect(state.model.BeginStroke({ MapBrush::StartMarker }).has_value(), "begin empty stroke");
    CancelMapEditorStroke(workspace, state);
    expect(!state.model.StrokeActive() && state.model.Revision() == modelRevision && workspace.Map().revision == draftRevision, "empty stroke cancellation changes neither revision");
    expect(state.model.BeginStroke({ MapBrush::StartMarker }).has_value() && state.model.Paint(31).value_or(false), "begin changed shell cancellation");
    CancelMapEditorStroke(workspace, state);
    expect(sameDocument(*state.model.Document(), before) && state.model.Revision() > modelRevision && workspace.Map().revision > draftRevision, "changed cancellation restores bytes and invalidates both revisions");
}

void testGridMinimapCornerAndLiveBytes()
{
    expect(MapTileOffset(0, 0) == 0, "top-left is file offset zero");
    expect(MapTileOffset(31, 31) == 1023, "bottom-right is file offset 1023");
    expect(MapTileCoordinates(1023) == MapTilePosition{ 31, 31 }, "corner offset maps back to row and column 31");
    expect(!MapTileCoordinates(1024) && !MapTileOffset(32, 0) && !MapTileOffset(0, 32), "grid bounds exclude footer and alternate sizes");
    MapEditModel model;
    expect(model.StartSynthetic().has_value(), "start live corner fixture");
    for (const float cellSize : { 2.0F, 6.0F, 16.0F, 48.0F })
    {
        const auto gridCorner = MapTileAtPoint(31.5F * cellSize, 31.5F * cellSize, cellSize);
        expect(gridCorner == 1023, "grid and minimap hit-test last cell identically at each scale");
        expect(!MapTileAtPoint(32.0F * cellSize, 31.5F * cellSize, cellSize), "right edge never aliases an adjacent row");
        expect(!MapTileAtPoint(31.5F * cellSize, 32.0F * cellSize, cellSize), "bottom edge never aliases footer");
    }
    expect(MapTileAtPoint(2.5F * 16, 0.5F * 16, 16) == MapTileOffset(2, 0) && MapTileAtPoint(0.5F * 16, 2.5F * 16, 16) == MapTileOffset(0, 2), "game view puts file rows on x and file columns on y");
    expect(!MapTileAtPoint(-1, 0, 4) && !MapTileAtPoint(0, 0, 0), "hit test excludes hidden cells and zero scale");
    expect(!MapTileAtPoint(std::numeric_limits<float>::infinity(), 0, 4), "nonfinite pointer is refused");
    const auto oldShade = MapTileShade(model.Document()->core[1023]);
    const auto oldColor = MapTilePaletteColor(model.Document()->core[1023]);
    expect(model.BeginStroke({ MapBrush::TerrainCode, 7 }).has_value(), "begin last-cell paint");
    expect(model.Paint(*MapTileAtPoint(31.5F * 16, 31.5F * 16, 16)).value_or(false), "paint last grid cell");
    const auto minimapOffset = *MapTileAtPoint(31.5F * 4, 31.5F * 4, 4);
    expect(model.Document()->core[minimapOffset] == std::byte{ 0x07 }, "minimap reads live byte before stroke commits");
    expect(MapTileShade(model.Document()->core[minimapOffset]) != oldShade, "minimap numeric shade updates during stroke");
    expect(MapTilePaletteColor(model.Document()->core[minimapOffset]) != oldColor, "shared grid and minimap palette updates during a live stroke");
    expect(MapTileShade(std::byte{ 0xFF }) == MapTileShade(std::byte{ 0x0F }), "unknown and flag bits imply no color semantics");
    expect(model.EndStroke() && model.Undo(), "undo corner stroke");
    expect(model.Document()->core[1023] == std::byte{ 0 } && model.SelectedTile() == 1023, "undo restores corner byte without rotating selection");
}

void testOrientationLabelPersists()
{
    MapEditModel model;
    const auto   visible = [&model]()
    {
        expect(model.OrientationLabel() == "game view: file rows left to right, file columns top to bottom", "canvas and minimap label names the game view");
    };
    visible();
    expect(model.ImportBytes(syntheticEnvelope()).has_value(), "import orientation fixture");
    visible();
    expect(model.BeginStroke({ MapBrush::TerrainCode, 3 }).has_value(), "begin orientation fixture stroke");
    expect(model.Paint(1023).has_value(), "paint orientation fixture");
    visible();
    static_cast<void>(model.EndStroke());
    static_cast<void>(model.Undo());
    visible();
    static_cast<void>(model.Redo());
    visible();
    model.Reset();
    visible();
}

void testCategoricalPaletteAndBitBreakdown()
{
    const auto& palette = MapTilePalette();
    expect(palette.size() == 16, "palette covers all sixteen numeric codes");
    for (std::size_t code = 0; code < palette.size(); ++code)
    {
        for (std::size_t other = code + 1; other < palette.size(); ++other)
        {
            expect(palette[code] != palette[other], "each numeric code has a distinct color");
        }
        expect(MapTilePaletteColor(static_cast<std::byte>(code | 0xF0U)) == palette[code], "high-bit overlays preserve the categorical base color");
    }
    expect(MapTilePaletteColor(std::byte{ 0x30 }) == palette[0], "raw bits retain a visible category zero");
    expect(FormatMapTileBits(std::byte{ 0x37 }) == "Bits 0-3: 0x7\nSet bits: 0, 1, 2, 4, 5\nraw bit 0x10: set\nraw bit 0x20: set\nFlag 0x40: clear\nFlag 0x80: clear", "0x37 lists its exact raw and accepted bits");
    expect(FormatMapTileBits(std::byte{ 0xF5 }) == "Bits 0-3: 0x5\nSet bits: 0, 2, 4, 5, 6, 7\nraw bit 0x10: set\nraw bit 0x20: set\nFlag 0x40: set\nFlag 0x80: set", "0xF5 lists its exact raw and accepted bits");
}

void testNoWriterPathAndIndependentWorkspace()
{
    CreatorWorkspace workspace;
    MapEditModel     model;
    expect(workspace.StartMapDraft("Synthetic import").has_value() && model.ImportBytes(syntheticEnvelope()).has_value(), "start M1 without output");
    expect(workspace.SelectMapStep(MapStep::EditGrid).has_value(), "continue to M2");
    workspace.SetInspection(InspectionSelection{ 123, 7, 4 });
    const auto source = *model.Source();
    expect(model.BeginStroke({ MapBrush::StartMarker }).has_value(), "begin M2 brush");
    expect(model.Paint(1023).has_value() && model.EndStroke(), "edit M2 bytes");
    expect(workspace.ReviseMap().has_value(), "edit invalidates artifact readiness");
    expect(workspace.SelectMapTile(1023).has_value(), "share selected tile with journey state");
    expect(model.SelectedTile() == 1023 && workspace.Map().selectedTile == 1023, "the model and draft share the selected tile");
    expect(workspace.Inspection() == InspectionSelection{ 123, 7, 4 }, "painting leaves independent inspection identity unchanged");
    expect(sameDocument(*model.Source(), source), "M1 and M2 leave immutable source bytes exact");
    expect(workspace.Map().readiness.ready == 0, "painting cannot mark writer/read-back ready");
    expect(std::ranges::none_of(workspace.Map().artifacts, [](const auto& artifact)
                                {
                                    return artifact.ready;
                                }),
           "no M1/M2 operation produces an output receipt");
    expect(model.Undo() && model.Redo() && sameDocument(*model.Source(), source), "edit history is entirely in memory");
    workspace.RequestDiscard();
    expect(workspace.ConfirmDiscard().has_value(), "explicit discard resets the map draft");
    model.Reset();
    expect(!model.Document() && !model.Source(), "discard releases imported snapshot and editable bytes");
    expect(workspace.Inspection() == InspectionSelection{ 123, 7, 4 }, "discard preserves independent inspection");
}

void testNativeCoordinatesAndTerrainNames()
{
    expect(MapNativeCell(0, 0) == MapNativeCellPosition{ 0, 31, 31 }, "first file tile maps to native (0,31), index 31");
    expect(MapNativeCell(0, 31) == MapNativeCellPosition{ 0, 0, 0 }, "file (0,31) maps to native origin");
    expect(MapNativeCell(31, 0) == MapNativeCellPosition{ 31, 31, 1271 }, "file (31,0) maps to native (31,31)");
    expect(MapNativeCell(31, 31) == MapNativeCellPosition{ 31, 0, 1240 }, "last file tile maps to native (31,0)");
    expect(MapNativeCell(7, 12) == MapNativeCellPosition{ 7, 19, 299 }, "interior mapping uses stride 40, not file stride 32");
    expect(!MapNativeCell(32, 0) && !MapNativeCell(0, 32) && !MapNativeCell(std::numeric_limits<std::size_t>::max(), 0), "native mapping rejects out-of-range coordinates before arithmetic");
    for (const auto& [code, name] : std::array<std::pair<std::uint8_t, std::string_view>, 6>{ { { 0, "sea" }, { 2, "plains" }, { 3, "forest" }, { 4, "hill" }, { 5, "desert" }, { 6, "mountain" } } })
    {
        expect(MapTerrainName(code) == name, "terrain name matches the canonical Research discriminator");
    }
    for (const auto code : { 1, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 })
    {
        expect(!MapTerrainName(static_cast<std::uint8_t>(code)), "unnamed values and out-of-nibble code 16 have no inferred name");
    }
    expect(MapTerrainInferredName(1) == std::string_view("grassland") && MapTerrainInferredName(7) == std::string_view("polar ice"), "retail usage infers grassland and polar ice");
    for (const auto code : { 0, 2, 6, 8, 15, 16 })
    {
        expect(!MapTerrainInferredName(static_cast<std::uint8_t>(code)), "only codes 1 and 7 carry inferred names");
    }
    expect(kMapTerrainNameSource == "names from Research terrain discriminator" && kMapTileEvidenceLabel == "static version 1.3 evidence", "captions retain source and static image scope");
}

void testStructuredBitDescriptions()
{
    const auto descriptions = DescribeMapTileBits(std::byte{ 0xF2 });
    expect(descriptions.size() == 4, "description includes all four high bits for a legend");
    expect(descriptions[0].fileBit == 0x10 && descriptions[1].fileBit == 0x20 && descriptions[2].fileBit == 0x40 && descriptions[3].fileBit == 0x80, "structured descriptions identify the file masks");
    expect(std::ranges::all_of(descriptions, [](const auto& description)
                               {
                                   return description.set;
                               }),
           "each set high bit is reported");
    expect(!descriptions[0].pairedWrite && !descriptions[1].pairedWrite, "halfword and marker writes do not imply a paired edge write");
    expect(descriptions[2].pairedWrite == MapPairedWrite{ 0x2, 0x8, 40, 1, 0 }, "0x40 preserves corrected own and paired edge masks");
    expect(descriptions[3].pairedWrite == MapPairedWrite{ 0x4, 0x1, -1, 0, 1 }, "0x80 preserves corrected own and paired edge masks");
    const auto origin = *MapNativeCell(7, 12);
    for (const auto& description : descriptions)
    {
        if (description.pairedWrite)
        {
            const auto pair     = *description.pairedWrite;
            const auto neighbor = *MapNativeCell(static_cast<std::size_t>(7 + pair.fileRowDelta), static_cast<std::size_t>(12 + pair.fileColumnDelta));
            expect(static_cast<int>(neighbor.index) == static_cast<int>(origin.index) + pair.nativeIndexDelta, "interior paired native write lands on the described file neighbor");
        }
    }
    for (std::size_t bit = 0; bit < descriptions.size(); ++bit)
    {
        const auto single = DescribeMapTileBits(static_cast<std::byte>(descriptions[bit].fileBit));
        for (std::size_t other = 0; other < single.size(); ++other)
        {
            expect(single[other].set == (bit == other), "each bit has independent set/clear state");
        }
    }
    const auto sea = DescribeMapTileBits(std::byte{ 0x10 });
    expect(sea[0].message.contains("Terrain 0 skips") && descriptions[0].message.contains("counter") && descriptions[0].message.contains("0x400 clear"), "0x10 text retains terrain exception and qualified side effects");
    expect(descriptions[0].message.contains("0x400 suppresses that arm") && descriptions[0].message.contains("initial state alone does not trigger it"), "loader-written nonzero-terrain flags do not alone trigger the negative-resource arm");
    expect(sea[0].message.contains("conditionally clears") && descriptions[0].message.contains("conditionally clears"), "both terrain branches retain conditional artifact consumption");
    expect(descriptions[1].message.contains("first eight") && descriptions[1].message.contains("Player assignment"), "marker description preserves limit and assignment uncertainty");
    expect(std::ranges::none_of(DescribeMapTileBits(std::byte{ 0 }), [](const auto& description)
                                {
                                    return description.set;
                                }),
           "clear bits remain describable without implying they are set");
}

void testDraftWarnings()
{
    MapDocument document{};
    expect(MapDraftWarnings(document).empty(), "clean synthetic sea grid has no advisory warnings");
    for (const auto bit : { 0x40, 0x80 })
    {
        for (const std::size_t axis : { 0U, 30U, 31U })
        {
            for (const std::size_t endpoint : { 0U, 31U })
            {
                document              = {};
                const auto offset     = *MapTileOffset(bit == 0x40 ? axis : endpoint, bit == 0x40 ? endpoint : axis);
                document.core[offset] = static_cast<std::byte>(bit);
                const auto before     = document;
                const auto warnings   = MapDraftWarnings(document);
                expect(warnings.size() == (axis == 31 ? 1U : 0U), "paired-write warning uses the correct file axis at both opposite edges and endpoints");
                if (!warnings.empty())
                {
                    expect(warnings[0].kind == MapTileWarningKind::DirectionalPairOutsideGrid && warnings[0].tileOffset == offset, "edge warning identifies the directional source tile");
                    expect(warnings[0].message.contains(bit == 0x40 ? "i+40" : "i-1"), "edge warning identifies the actual native paired write");
                    if (bit == 0x80 && endpoint == 0)
                    {
                        expect(warnings[0].message.contains("E[-1] before the byte edge plane") && warnings[0].message.contains("file (0,31)"), "row-zero column-31 warning identifies the write before the plane");
                    }
                }
                expect(sameDocument(document, before), "warning inspection preserves every source byte");
            }
        }
    }
    document            = {};
    document.core[1023] = std::byte{ 0xC9 };
    const auto combined = MapDraftWarnings(document);
    expect(combined.size() == 3, "combined corner flags and unnamed terrain produce exactly three warnings");
    if (combined.size() == 3)
    {
        expect(combined[0].kind == MapTileWarningKind::DirectionalPairOutsideGrid && combined[0].message.contains("0x40"), "combined warnings report the 0x40 pair first");
        expect(combined[1].kind == MapTileWarningKind::DirectionalPairOutsideGrid && combined[1].message.contains("0x80"), "combined warnings report the 0x80 pair second");
        expect(combined[2].kind == MapTileWarningKind::UnnamedTerrain, "combined warnings report unnamed terrain third");
        expect(std::ranges::all_of(combined, [](const auto& warning)
                                   {
                                       return warning.tileOffset == 1023;
                                   }),
               "all combined warnings identify the same source tile");
    }

    document = {};
    for (std::size_t marker = 0; marker < 8; ++marker)
    {
        document.core[marker] = std::byte{ 0x20 };
    }
    expect(kMapMarkerLimit == 8 && MapDraftWarnings(document).empty(), "exactly eight markers do not exceed the loader capacity");
    document.core[32]  = std::byte{ 0x20 };
    const auto markers = MapDraftWarnings(document);
    expect(markers.size() == 1 && markers[0].kind == MapTileWarningKind::MarkerLimitExceeded && markers[0].tileOffset == 32, "row-major order ignores row 1 column 0; column-major would ignore row 0 column 7");
    document.core[33]       = std::byte{ 0x20 };
    const auto extraMarkers = MapDraftWarnings(document);
    expect(extraMarkers.size() == 2 && extraMarkers[0].kind == MapTileWarningKind::MarkerLimitExceeded && extraMarkers[0].tileOffset == 32 && extraMarkers[1].kind == MapTileWarningKind::MarkerLimitExceeded && extraMarkers[1].tileOffset == 33, "row-major order ignores offsets 32 then 33; column-major would ignore offsets 6 and 7");
    document.core[33] = std::byte{ 0 };
    document.core[0]  = std::byte{ 0 };
    expect(MapDraftWarnings(document).empty(), "removing an earlier marker admits the previously ninth marker");

    document          = {};
    document.core[17] = std::byte{ 0x10 };
    const auto sea    = MapDraftWarnings(document);
    expect(sea.size() == 1 && sea[0].kind == MapTileWarningKind::Bit10OnSea && sea[0].tileOffset == 17, "terrain zero with file 0x10 has distinct handling");
    document.core[17] = std::byte{ 0x12 };
    expect(MapDraftWarnings(document).empty(), "nonzero named terrain with 0x10 does not receive the sea exception warning");
    document.core[17]       = std::byte{ 0x19 };
    const auto unnamedBit10 = MapDraftWarnings(document);
    expect(unnamedBit10.size() == 1 && unnamedBit10[0].kind == MapTileWarningKind::UnnamedTerrain && unnamedBit10[0].tileOffset == 17, "terrain 9 with 0x10 receives only the unnamed warning, not the terrain-zero warning");

    document          = {};
    document.core[18] = std::byte{ 1 };
    document.core[20] = std::byte{ 7 };
    expect(MapDraftWarnings(document).empty(), "inferred grassland and polar ice do not warn as unnamed");
    document.core[19]  = std::byte{ 9 };
    const auto unnamed = MapDraftWarnings(document);
    expect(unnamed.size() == 1 && unnamed[0].kind == MapTileWarningKind::UnnamedTerrain && unnamed[0].tileOffset == 19, "unnamed low nibble warns at its tile without rejecting bytes");
    expect(unnamed[0].message.contains("does not establish authoring validity"), "unnamed warning does not equate missing names with invalid terrain");

    document = {};
    expect(MapStartMarkerCount(document) == 0 && MapStartMarkerAdvice(document) == std::optional<std::string>("0 start markers. Retail maps have five. A player without one starts at leftover coordinates."), "a map without markers gets the retail count advice");
    document.core[40] = std::byte{ 0x22 };
    expect(MapStartMarkerAdvice(document).value_or("").starts_with("1 start marker."), "one marker uses the singular");
    for (const std::size_t offset : { 41U, 42U, 43U, 44U })
    {
        document.core[offset] = std::byte{ 0x21 };
    }
    expect(MapStartMarkerCount(document) == 5 && !MapStartMarkerAdvice(document), "five markers match retail maps and need no advice");
}

} // namespace

int main()
{
    testStartAndExactImport();
    testBrushMasksPreserveRawBitsAndFooter();
    testStrokeTransactionsAndHistory();
    testReplaceTiles();
    testInvalidEditsKeepBytesAndSelection();
    testCancelInterruptedStrokeRestoresImportedBytes();
    testNoOpStrokeCancelPreservesRevision();
    testGridMinimapCornerAndLiveBytes();
    testOrientationLabelPersists();
    testCategoricalPaletteAndBitBreakdown();
    testNoWriterPathAndIndependentWorkspace();
    testNativeCoordinatesAndTerrainNames();
    testStructuredBitDescriptions();
    testDraftWarnings();
    if (failures != 0)
    {
        std::cerr << failures << " map editor assertion(s) failed.\n";
        return 1;
    }
    std::cout << "Map editor tests passed.\n";
    return 0;
}
