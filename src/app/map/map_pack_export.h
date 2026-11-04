#pragma once

#include "map/creator_workspace.h"
#include "map/map_draft_store.h"
#include "map_document.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rerevved::tools
{

inline constexpr std::uint32_t    kMapPackFirstSlot       = 5;
inline constexpr std::uint32_t    kMapPackLastSlot        = 31;
inline constexpr std::int32_t     kMapPackFirstScenarioId = 100;
inline constexpr std::size_t      kMapPackMaxIdLength     = 39;
inline constexpr std::size_t      kMapPackMaxArchiveBytes = 256 * 1024 * 1024; // the title's limit
inline constexpr std::string_view kMapPackAssetKey        = "map-pack/dlc.fpk";

struct MapPackSettings
{
    std::string   id;
    std::string   name;
    std::string   version = "1.0";
    std::string   author;      // optional: empty means omitted
    std::string   description; // optional: empty means omitted
    std::uint32_t slot = kMapPackFirstSlot;
};

struct MapPackAssembly
{
    std::string              mapName;    // the MAP names, comma separated
    std::vector<std::string> entryNames; // archive order
    std::vector<std::byte>   archive;    // encoded, dlc.fpk verified by the parser
    std::string              sha256;     // lowercase hex of archive
};

// In display order. Empty when ready.
[[nodiscard]] std::vector<std::string>                    MapPackBlockers(const MapDraft& draft, const MapDocument* map, const MapPackSettings& settings);
[[nodiscard]] std::expected<MapPackAssembly, std::string> AssembleMapPack(const MapDraft& draft, const MapDocument& map, const MapPackSettings& settings);

// Every map has its own MAP name and scenario ID.
struct MapPackMap
{
    const MapDraft*    draft = nullptr;
    const MapDocument* map   = nullptr;
};

// One DLCScenarioData<slot>.xml holds the entries of every map, in order.
[[nodiscard]] std::expected<MapPackAssembly, std::string> AssembleMapPack(std::span<const MapPackMap> maps, const MapPackSettings& settings);

struct MapPackReceipt
{
    bool                  published = false;
    std::uint64_t         revision  = 0;
    std::string           identity;
    std::filesystem::path packDirectory; // mapsRoot / id (set even on failure when known)
    std::string           mapName;
    std::uint32_t         slot         = 0;
    std::size_t           archiveBytes = 0;
    std::string           sha256;
    std::string           diagnostic; // one or more lines. On success includes install steps
};

// Writes into the existing directory mapsRoot, never replacing, and reads dlc.fpk back byte for byte.
// Refuses before writing when the pack folder overlaps a protected root.
[[nodiscard]] MapPackReceipt ExportMapPack(const MapDraft&                        draft,
                                           const MapDocument&                     map,
                                           const MapPackSettings&                 settings,
                                           const std::filesystem::path&           mapsRoot,
                                           std::span<const std::filesystem::path> protectedRoots = {});
[[nodiscard]] MapPackReceipt ExportMapPack(std::span<const MapPackMap>            maps,
                                           const MapPackSettings&                 settings,
                                           const std::filesystem::path&           mapsRoot,
                                           std::span<const std::filesystem::path> protectedRoots = {});
[[nodiscard]] std::string    FormatMapPackReceipt(const MapPackReceipt& receipt);

// Images are generated from the tiles again, as for a map file.
[[nodiscard]] std::expected<SavedMapDraft, std::string> LoadMapPack(const std::filesystem::path& path);

} // namespace rerevved::tools
