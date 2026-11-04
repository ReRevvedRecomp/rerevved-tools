#include "map/map_edit_model.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

namespace rerevved::tools
{

const std::array<MapTileColor, 16>& MapTilePalette() noexcept
{
    // Codes 8-15 have no game terrain and keep distinct categorical colors.
    static constexpr std::array<MapTileColor, 16> palette{ {
        { 52, 104, 160 },  // sea
        { 108, 176, 72 },  // grassland
        { 196, 184, 104 }, // plains
        { 40, 110, 60 },   // forest
        { 150, 120, 80 },  // hill
        { 226, 200, 140 }, // desert
        { 128, 122, 118 }, // mountain
        { 232, 240, 246 }, // polar ice
        { 105, 173, 177 },
        { 175, 184, 100 },
        { 161, 120, 94 },
        { 151, 173, 221 },
        { 219, 174, 145 },
        { 157, 203, 199 },
        { 193, 157, 194 },
        { 210, 210, 210 },
    } };
    return palette;
}

MapTileColor MapTilePaletteColor(std::byte value) noexcept
{
    return MapTilePalette()[std::to_integer<unsigned int>(value) & 0x0FU];
}

std::optional<MapNativeCellPosition> MapNativeCell(std::size_t row, std::size_t column) noexcept
{
    if (!MapTileOffset(row, column))
    {
        return std::nullopt;
    }
    // xbox-map-tile-semantics-boundary /orientation/mapping uses i = 40*x + y.
    const auto y = kMapWidth - 1 - column;
    return MapNativeCellPosition{ row, y, 40 * row + y };
}

std::optional<std::string_view> MapTerrainName(std::uint8_t code) noexcept
{
    // unique-era-ability-effect-ownership /terrainYieldBoundary/terrainDiscriminator.
    switch (code)
    {
        case 0:
            return "sea";
        case 2:
            return "plains";
        case 3:
            return "forest";
        case 4:
            return "hill";
        case 5:
            return "desert";
        case 6:
            return "mountain";
        default:
            return std::nullopt;
    }
}

std::optional<std::string_view> MapTerrainInferredName(std::uint8_t code) noexcept
{
    // Retail DLC maps: 122 of 150 start markers sit on code 1, which carries the
    // grassland resources. Code 7 fills the two outer file columns on each side.
    switch (code)
    {
        case 1:
            return "grassland";
        case 7:
            return "polar ice";
        default:
            return std::nullopt;
    }
}

std::size_t MapStartMarkerCount(const MapDocument& document) noexcept
{
    return static_cast<std::size_t>(std::ranges::count_if(document.core, [](std::byte value)
                                                          {
                                                              return (std::to_integer<unsigned int>(value) & 0x20U) != 0;
                                                          }));
}

std::optional<std::string> MapStartMarkerAdvice(const MapDocument& document)
{
    const auto count = MapStartMarkerCount(document);
    if (count >= kMapRetailMarkerCount)
    {
        return std::nullopt;
    }
    // Setup reads marker n for player n and never fills slots past the authored count.
    return std::format("{} start marker{}. Retail maps have five. A player without one starts at "
                       "leftover coordinates.",
                       count,
                       count == 1 ? "" : "s");
}

std::vector<std::string> MapWarningSummaries(const MapDocument& document)
{
    constexpr std::array<std::pair<MapTileWarningKind, std::string_view>, 4> kSummaries{ {
        { MapTileWarningKind::DirectionalPairOutsideGrid, "with edge flags that also write outside the grid" },
        { MapTileWarningKind::MarkerLimitExceeded, "with start markers after the first eight, which the game ignores" },
        { MapTileWarningKind::Bit10OnSea, "of sea with flag 0x10, which skips its extra write" },
        { MapTileWarningKind::UnnamedTerrain, "with terrain codes that have no name" },
    } };
    const auto                                                               warnings = MapDraftWarnings(document);
    std::vector<std::string>                                                 summaries;
    for (const auto& [kind, summary] : kSummaries)
    {
        // Count tiles, not warnings: a corner tile can carry both edge warnings.
        std::optional<std::size_t> previous;
        std::size_t                count = 0;
        for (const auto& warning : warnings)
        {
            if (warning.kind == kind && warning.tileOffset != previous)
            {
                ++count;
                previous = warning.tileOffset;
            }
        }
        if (count != 0)
        {
            summaries.push_back(std::format("{} tile{} {}.", count, count == 1 ? "" : "s", summary));
        }
    }
    return summaries;
}

std::vector<MapBitDescription> DescribeMapTileBits(std::byte value)
{
    const auto byte = std::to_integer<unsigned int>(value);
    // xbox-map-tile-semantics-boundary /highBits keeps the three native planes distinct.
    return {
        { 0x10,
          (byte & 0x10U) != 0,
          std::nullopt,
          (byte & 0x0FU) == 0
              ? "Sets halfword flag 0x400 and clears 0x80 on this cell. Terrain 0 skips the "
                "additional 0x10 write. A selected branch that checks artifact presence tests "
                "0x400. Separate selected consumption conditionally clears it. Complete effects "
                "remain unresolved."
              : "Sets halfword flags 0x400 and 0x10 and clears 0x80 on this cell. It also "
                "increments a loader counter and advances linear congruential state. A selected "
                "branch that checks artifact presence tests 0x400. Separate selected consumption "
                "conditionally clears it. A selected resource callback's arm for negative "
                "resources requires 0x10 set and 0x400 clear. The loader written 0x400 suppresses "
                "that arm, so this initial state alone does not trigger it. Complete effects "
                "remain unresolved." },
        { 0x20,
          (byte & 0x20U) != 0,
          std::nullopt,
          "Appends (31-column, row) to the two coordinate arrays with eight entries for the first "
          "eight markers in file order. Later markers are ignored. No deduplication or terrain "
          "predicate. This is a storage boundary for starting coordinates. Player assignment, "
          "placement and unmarked entries remain unresolved. This is not halfword flag 0x20." },
        { 0x40,
          (byte & 0x40U) != 0,
          MapPairedWrite{ 0x2, 0x8, 40, 1, 0 },
          "ORs byte edge flag 0x2 on native cell i and 0x8 on i+40: file tile (row+1, column). No "
          "local clipping or wrapping. Geometry and safe combinations remain unresolved." },
        { 0x80,
          (byte & 0x80U) != 0,
          MapPairedWrite{ 0x4, 0x1, -1, 0, 1 },
          "ORs byte edge flag 0x4 on native cell i and 0x1 on i-1: file tile (row, column+1). No "
          "local clipping or wrapping. Geometry and safe combinations remain unresolved." },
    };
}

std::vector<MapTileWarning> MapDraftWarnings(const MapDocument& document)
{
    std::vector<MapTileWarning> warnings;
    std::size_t                 markers = 0;
    for (std::size_t offset = 0; offset < document.core.size(); ++offset)
    {
        const auto byte     = std::to_integer<unsigned int>(document.core[offset]);
        const auto terrain  = static_cast<std::uint8_t>(byte & 0x0FU);
        const auto position = *MapTileCoordinates(offset);
        // xbox-map-tile-semantics-boundary /highBits/edgePairs/edges: no clipping or wrapping.
        // i+40 reaches padding row 32, and y=0 makes i-1 cross a row, including E[-1] at x=0.
        if ((byte & 0x40U) != 0 && position.row == kMapHeight - 1)
        {
            warnings.push_back({ MapTileWarningKind::DirectionalPairOutsideGrid,
                                 offset,
                                 "0x40 paired write i+40 reaches native padding row 32, outside "
                                 "the 32x32 grid (file row+1). No local clipping or wrapping." });
        }
        if ((byte & 0x80U) != 0 && position.column == kMapWidth - 1)
        {
            warnings.push_back({ MapTileWarningKind::DirectionalPairOutsideGrid,
                                 offset,
                                 position.row == 0
                                     ? "0x80 paired write i-1 reaches E[-1] before the byte edge "
                                       "plane at file (0,31). No local clipping or wrapping."
                                     : "0x80 paired write i-1 crosses the native row boundary "
                                       "outside the 32x32 grid. No local clipping or wrapping." });
        }
        if ((byte & 0x20U) != 0 && ++markers > kMapMarkerLimit)
        {
            warnings.push_back({ MapTileWarningKind::MarkerLimitExceeded, offset, "0x20 marker is ignored: the loader appends only the first eight markers in file order." });
        }
        if ((byte & 0x10U) != 0 && terrain == 0)
        {
            warnings.push_back({ MapTileWarningKind::Bit10OnSea, offset, "Terrain 0 with file bit 0x10 sets halfword 0x400 and clears 0x80, but skips the additional halfword 0x10 write." });
        }
        if (!MapTerrainName(terrain) && !MapTerrainInferredName(terrain))
        {
            warnings.push_back(
                { MapTileWarningKind::UnnamedTerrain,
                  offset,
                  std::format("Terrain code 0x{:X} has no Research name. Nibble admission does "
                              "not establish authoring validity or final retained terrain.",
                              terrain) });
        }
    }
    return warnings;
}

std::string FormatMapTileBits(std::byte value)
{
    const auto  byte   = std::to_integer<unsigned int>(value);
    std::string result = std::format("Bits 0-3: 0x{:X}\nSet bits: ", byte & 0x0FU);
    bool        any    = false;
    for (unsigned int bit = 0; bit < 8; ++bit)
    {
        if ((byte & (1U << bit)) != 0)
        {
            result += (any ? ", " : "") + std::to_string(bit);
            any = true;
        }
    }
    if (!any)
    {
        result += "none";
    }
    for (const auto& [mask, label] : std::array<std::pair<unsigned int, const char*>, 4>{ { { 0x10U, "raw bit 0x10" }, { 0x20U, "raw bit 0x20" }, { 0x40U, "Flag 0x40" }, { 0x80U, "Flag 0x80" } } })
    {
        result += '\n';
        result += label;
        result += (byte & mask) != 0 ? ": set" : ": clear";
    }
    return result;
}

std::string_view MapEditErrorMessage(MapEditError error)
{
    switch (error)
    {
        case MapEditError::NoDraft:
            return "Start a new draft or import a 1088 byte MAP first.";
        case MapEditError::DraftExists:
            return "Discard the current map draft explicitly before starting another.";
        case MapEditError::InvalidTile:
            return "Choose a file order offset from 0 through 1023, or row and column from 0 "
                   "through 31.";
        case MapEditError::InvalidTerrainCode:
            return "Terrain code must be 0 through 7, the codes the game has terrain for.";
        case MapEditError::InvalidBrush:
            return "Choose Select tile, Terrain code or Start marker.";
        case MapEditError::StrokeActive:
            return "Finish or cancel the current stroke before starting another.";
        case MapEditError::NoStroke:
            return "Begin a stroke before applying the brush.";
    }
    return "Unknown map edit error.";
}

std::uint8_t MapBrushMask(MapBrush brush) noexcept
{
    switch (brush)
    {
        case MapBrush::TerrainCode:
            return 0x0F;
        case MapBrush::StartMarker:
            return 0x20;
        case MapBrush::SelectTile:
            return 0;
    }
    return 0;
}

std::optional<std::size_t> MapTileOffset(std::size_t row, std::size_t column) noexcept
{
    if (row >= kMapHeight || column >= kMapWidth)
    {
        return std::nullopt;
    }
    return row * kMapWidth + column;
}

std::optional<MapTilePosition> MapTileCoordinates(std::size_t offset) noexcept
{
    if (offset >= kMapCoreSize)
    {
        return std::nullopt;
    }
    return MapTilePosition{ offset / kMapWidth, offset % kMapWidth };
}

std::optional<std::size_t> MapTileAtPoint(float x, float y, float cellSize) noexcept
{
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(cellSize) || cellSize <= 0 || x < 0 || y < 0)
    {
        return std::nullopt;
    }
    // Game view: file row runs along x and file column along y.
    const float row    = x / cellSize;
    const float column = y / cellSize;
    if (column >= static_cast<float>(kMapWidth) || row >= static_cast<float>(kMapHeight))
    {
        return std::nullopt;
    }
    return MapTileOffset(static_cast<std::size_t>(row), static_cast<std::size_t>(column));
}

std::uint8_t MapTileShade(std::byte value) noexcept
{
    return static_cast<std::uint8_t>(20U + (std::to_integer<unsigned int>(value) & 0x0FU) * 15U);
}

std::expected<void, MapEditError> MapEditModel::StartSynthetic()
{
    MapDocument seed{};
    seed.footer.fill(std::byte{ 0xFF });
    return StartNew(seed);
}

std::expected<void, MapEditError> MapEditModel::StartNew(MapDocument seed)
{
    auto result = StartImported(std::move(seed));
    if (result)
    {
        synthetic = true;
    }
    return result;
}

std::expected<void, MapEditError> MapEditModel::StartImported(MapDocument value)
{
    if (document)
    {
        return std::unexpected(MapEditError::DraftExists);
    }
    source    = value;
    document  = std::move(value);
    synthetic = false;
    ++revision;
    return {};
}

std::expected<void, MapEditError> MapEditModel::StartSaved(MapDocument origin, MapDocument edited, bool fromBlank)
{
    auto result = StartImported(std::move(origin));
    if (result)
    {
        document  = std::move(edited);
        synthetic = fromBlank;
    }
    return result;
}

std::expected<void, std::string> MapEditModel::ImportBytes(std::span<const std::byte> bytes)
{
    if (document)
    {
        return std::unexpected(std::string(MapEditErrorMessage(MapEditError::DraftExists)));
    }
    auto parsed = ParseMapDocument(bytes);
    if (!parsed)
    {
        return std::unexpected(std::string(MapDocumentErrorMessage(parsed.error())));
    }
    static_cast<void>(StartImported(std::move(*parsed)));
    return {};
}

void MapEditModel::Reset() noexcept
{
    source.reset();
    document.reset();
    selectedTile.reset();
    strokeBefore.reset();
    history.clear();
    historyPosition = 0;
    synthetic       = false;
    ++revision;
}

const MapDocument* MapEditModel::Document() const noexcept
{
    return document ? &*document : nullptr;
}

const MapDocument* MapEditModel::Source() const noexcept
{
    return source ? &*source : nullptr;
}

bool MapEditModel::Synthetic() const noexcept
{
    return synthetic;
}

std::uint64_t MapEditModel::Revision() const noexcept
{
    return revision;
}

std::string_view MapEditModel::OrientationLabel() const noexcept
{
    return kMapOrientationLabel;
}

std::optional<std::size_t> MapEditModel::SelectedTile() const noexcept
{
    return selectedTile;
}

std::expected<void, MapEditError> MapEditModel::SelectTile(std::size_t offset)
{
    if (!document)
    {
        return std::unexpected(MapEditError::NoDraft);
    }
    if (!MapTileCoordinates(offset))
    {
        return std::unexpected(MapEditError::InvalidTile);
    }
    selectedTile = offset;
    return {};
}

std::expected<void, MapEditError> MapEditModel::BeginStroke(MapBrushSettings settings)
{
    if (!document)
    {
        return std::unexpected(MapEditError::NoDraft);
    }
    if (strokeBefore)
    {
        return std::unexpected(MapEditError::StrokeActive);
    }
    switch (settings.tool)
    {
        case MapBrush::SelectTile:
        case MapBrush::StartMarker:
            break;
        case MapBrush::TerrainCode:
            if (settings.terrainCode > kMapLastTerrainCode)
            {
                return std::unexpected(MapEditError::InvalidTerrainCode);
            }
            break;
        default:
            return std::unexpected(MapEditError::InvalidBrush);
    }
    brush        = settings;
    strokeBefore = document->core;
    return {};
}

std::expected<bool, MapEditError> MapEditModel::Paint(std::size_t offset)
{
    if (!document)
    {
        return std::unexpected(MapEditError::NoDraft);
    }
    if (!strokeBefore)
    {
        return std::unexpected(MapEditError::NoStroke);
    }
    auto result = SelectTile(offset);
    if (!result)
    {
        return std::unexpected(result.error());
    }
    const auto mask        = MapBrushMask(brush.tool);
    const auto value       = std::to_integer<std::uint8_t>(document->core[offset]);
    const auto bits        = brush.tool == MapBrush::TerrainCode ? brush.terrainCode : (brush.set ? mask : 0U);
    const auto replacement = static_cast<std::byte>((value & static_cast<unsigned int>(~mask & 0xFF)) | bits);
    if (replacement == document->core[offset])
    {
        return false;
    }
    document->core[offset] = replacement;
    ++revision;
    return true;
}

bool MapEditModel::EndStroke()
{
    if (!strokeBefore)
    {
        return false;
    }
    std::vector<TileChange> changes;
    for (std::size_t offset = 0; offset < kMapCoreSize; ++offset)
    {
        if ((*strokeBefore)[offset] != document->core[offset])
        {
            changes.push_back({ offset, (*strokeBefore)[offset], document->core[offset] });
        }
    }
    strokeBefore.reset();
    if (changes.empty())
    {
        return false;
    }
    history.resize(historyPosition);
    if (history.size() == kMapUndoLimit)
    {
        history.erase(history.begin());
    }
    history.push_back(std::move(changes));
    historyPosition = history.size();
    return true;
}

bool MapEditModel::CancelStroke()
{
    if (!strokeBefore)
    {
        return false;
    }
    const bool changed = document->core != *strokeBefore;
    document->core     = *strokeBefore;
    strokeBefore.reset();
    if (changed)
    {
        ++revision;
    }
    return changed;
}

bool MapEditModel::StrokeActive() const noexcept
{
    return strokeBefore.has_value();
}

std::expected<bool, MapEditError> MapEditModel::ReplaceTiles(const std::array<std::byte, kMapCoreSize>& core)
{
    if (!document)
    {
        return std::unexpected(MapEditError::NoDraft);
    }
    if (strokeBefore)
    {
        return std::unexpected(MapEditError::StrokeActive);
    }
    strokeBefore       = document->core;
    document->core     = core;
    const bool changed = EndStroke();
    if (changed)
    {
        ++revision;
    }
    return changed;
}

bool MapEditModel::CanUndo() const noexcept
{
    return historyPosition != 0;
}

bool MapEditModel::CanRedo() const noexcept
{
    return historyPosition < history.size();
}

bool MapEditModel::Undo()
{
    if (strokeBefore || !CanUndo())
    {
        return false;
    }
    for (const auto& change : history[--historyPosition])
    {
        document->core[change.offset] = change.before;
    }
    ++revision;
    return true;
}

bool MapEditModel::Redo()
{
    if (strokeBefore || !CanRedo())
    {
        return false;
    }
    for (const auto& change : history[historyPosition++])
    {
        document->core[change.offset] = change.after;
    }
    ++revision;
    return true;
}

} // namespace rerevved::tools
