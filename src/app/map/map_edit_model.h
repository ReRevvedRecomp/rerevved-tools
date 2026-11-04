#pragma once

#include "map_document.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rerevved::tools
{

inline constexpr std::size_t      kMapUndoLimit         = 128;
inline constexpr std::string_view kMapOrientationLabel  = "game view: file rows left to right, file columns top to bottom";
inline constexpr std::string_view kMapTerrainNameSource = "names from Research terrain discriminator";
inline constexpr std::string_view kMapTileEvidenceLabel = "static version 1.3 evidence";
inline constexpr std::size_t      kMapMarkerLimit       = 8;
inline constexpr std::size_t      kMapRetailMarkerCount = 5;
// Retail maps use only terrain codes 0-7. 8-15 have no game terrain, so the brush stops at 7.
inline constexpr std::uint8_t kMapLastTerrainCode = 7;

enum class MapBrush
{
    SelectTile,
    TerrainCode,
    StartMarker,
};

struct MapBrushSettings
{
    MapBrush     tool        = MapBrush::SelectTile;
    std::uint8_t terrainCode = 0;
    bool         set         = true;
};

enum class MapEditError
{
    NoDraft,
    DraftExists,
    InvalidTile,
    InvalidTerrainCode,
    InvalidBrush,
    StrokeActive,
    NoStroke,
};

struct MapTilePosition
{
    std::size_t row    = 0;
    std::size_t column = 0;

    [[nodiscard]] friend bool operator==(const MapTilePosition&, const MapTilePosition&) = default;
};

struct MapTileColor
{
    std::uint8_t red;
    std::uint8_t green;
    std::uint8_t blue;

    [[nodiscard]] friend bool operator==(const MapTileColor&, const MapTileColor&) = default;
};

struct MapNativeCellPosition
{
    std::size_t x;
    std::size_t y;
    std::size_t index;

    [[nodiscard]] friend bool operator==(const MapNativeCellPosition&, const MapNativeCellPosition&) = default;
};

// These masks belong to the byte edge plane, not the terrain or halfword flag planes.
struct MapPairedWrite
{
    std::uint8_t thisCellFlag;
    std::uint8_t neighborFlag;
    int          nativeIndexDelta;
    int          fileRowDelta;
    int          fileColumnDelta;

    [[nodiscard]] friend bool operator==(const MapPairedWrite&, const MapPairedWrite&) = default;
};

struct MapBitDescription
{
    std::uint8_t                  fileBit;
    bool                          set;
    std::optional<MapPairedWrite> pairedWrite;
    std::string_view              message;
};

enum class MapTileWarningKind
{
    DirectionalPairOutsideGrid,
    MarkerLimitExceeded,
    Bit10OnSea,
    UnnamedTerrain,
};

struct MapTileWarning
{
    MapTileWarningKind         kind;
    std::optional<std::size_t> tileOffset;
    std::string                message;
};

// Per xbox-map-tile-semantics-boundary the game draws native x (file row) left to right and file column top to bottom.
// The editor grid is the transpose of file order.
[[nodiscard]] std::optional<MapNativeCellPosition> MapNativeCell(std::size_t row, std::size_t column) noexcept;
[[nodiscard]] std::optional<std::string_view>      MapTerrainName(std::uint8_t code) noexcept;
// Names inferred from retail DLC map usage, not yet promoted by Research.
[[nodiscard]] std::optional<std::string_view> MapTerrainInferredName(std::uint8_t code) noexcept;
[[nodiscard]] std::size_t                     MapStartMarkerCount(const MapDocument& document) noexcept;
// Empty when the draft has at least as many start markers as every retail map.
[[nodiscard]] std::optional<std::string>     MapStartMarkerAdvice(const MapDocument& document);
[[nodiscard]] std::vector<MapBitDescription> DescribeMapTileBits(std::byte value);
[[nodiscard]] std::vector<MapTileWarning>    MapDraftWarnings(const MapDocument& document);
[[nodiscard]] std::vector<std::string>       MapWarningSummaries(const MapDocument& document);

[[nodiscard]] const std::array<MapTileColor, 16>& MapTilePalette() noexcept;
[[nodiscard]] MapTileColor                        MapTilePaletteColor(std::byte value) noexcept;
[[nodiscard]] std::string                         FormatMapTileBits(std::byte value);

[[nodiscard]] std::string_view               MapEditErrorMessage(MapEditError error);
[[nodiscard]] std::uint8_t                   MapBrushMask(MapBrush brush) noexcept;
[[nodiscard]] std::optional<std::size_t>     MapTileOffset(std::size_t row, std::size_t column) noexcept;
[[nodiscard]] std::optional<MapTilePosition> MapTileCoordinates(std::size_t offset) noexcept;
[[nodiscard]] std::optional<std::size_t>     MapTileAtPoint(float x, float y, float cellSize) noexcept;
[[nodiscard]] std::uint8_t                   MapTileShade(std::byte value) noexcept;

// Owns one session draft. Imports and edits never expose mutable source bytes or file output.
class MapEditModel
{
public:
    [[nodiscard]] std::expected<void, MapEditError> StartSynthetic();
    // A new map from generated tiles. Like StartSynthetic, it is not a copy of a file.
    [[nodiscard]] std::expected<void, MapEditError> StartNew(MapDocument seed);
    [[nodiscard]] std::expected<void, MapEditError> StartImported(MapDocument document);
    // Resumes a saved draft from its origin and edited bytes. Undo history starts empty.
    [[nodiscard]] std::expected<void, MapEditError> StartSaved(MapDocument origin, MapDocument edited, bool fromBlank);
    [[nodiscard]] std::expected<void, std::string>  ImportBytes(std::span<const std::byte> bytes);
    void                                            Reset() noexcept;

    [[nodiscard]] const MapDocument*                Document() const noexcept;
    [[nodiscard]] const MapDocument*                Source() const noexcept;
    [[nodiscard]] bool                              Synthetic() const noexcept;
    [[nodiscard]] std::uint64_t                     Revision() const noexcept;
    [[nodiscard]] std::string_view                  OrientationLabel() const noexcept;
    [[nodiscard]] std::optional<std::size_t>        SelectedTile() const noexcept;
    [[nodiscard]] std::expected<void, MapEditError> SelectTile(std::size_t offset);

    [[nodiscard]] std::expected<void, MapEditError> BeginStroke(MapBrushSettings brush);
    [[nodiscard]] std::expected<bool, MapEditError> Paint(std::size_t offset);
    [[nodiscard]] bool                              EndStroke();
    [[nodiscard]] bool                              CancelStroke();
    [[nodiscard]] bool                              StrokeActive() const noexcept;
    // Replaces every tile at once as one undo step. False when nothing changed.
    [[nodiscard]] std::expected<bool, MapEditError> ReplaceTiles(const std::array<std::byte, kMapCoreSize>& core);
    [[nodiscard]] bool                              CanUndo() const noexcept;
    [[nodiscard]] bool                              CanRedo() const noexcept;
    [[nodiscard]] bool                              Undo();
    [[nodiscard]] bool                              Redo();

private:
    struct TileChange
    {
        std::size_t offset;
        std::byte   before;
        std::byte   after;
    };

    std::optional<MapDocument>                         source;
    std::optional<MapDocument>                         document;
    bool                                               synthetic = false;
    std::uint64_t                                      revision  = 0;
    std::optional<std::size_t>                         selectedTile;
    MapBrushSettings                                   brush;
    std::optional<std::array<std::byte, kMapCoreSize>> strokeBefore;
    std::vector<std::vector<TileChange>>               history;
    std::size_t                                        historyPosition = 0;
};

} // namespace rerevved::tools
