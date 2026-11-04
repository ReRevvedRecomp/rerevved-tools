#pragma once

#include "map/creator_workspace.h"
#include "map_document.h"
#include "scenario_document.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rerevved::tools
{

inline constexpr std::string_view kMapDraftExtension = ".rrmap";
inline constexpr std::size_t      kRecentMapLimit    = 10;

struct SavedMapExport
{
    std::string           name;
    std::string           id;
    std::string           version = "1.0";
    std::string           author;
    std::string           description;
    bool                  idEdited      = false;
    bool                  namePrefilled = false;
    std::int32_t          slot          = 5;
    std::filesystem::path outputParent;

    bool operator==(const SavedMapExport&) const = default;
};

// Images are not saved. They are generated from the tiles again.
struct SavedMapDraft
{
    std::string identity;
    // Recovery copies only: the map file the unsaved changes belong to, if any.
    std::filesystem::path           file;
    MapStep                         step      = MapStep::EditGrid;
    bool                            synthetic = true;
    MapDocument                     source{};
    MapDocument                     tiles{};
    std::optional<ScenarioDocument> scenario;
    SavedMapExport                  exportForm;
    std::int64_t                    savedAt = 0; // seconds since the Unix epoch

    friend bool operator==(const SavedMapDraft& left, const SavedMapDraft& right);
};

struct MapDraftListing
{
    std::filesystem::path path;
    // Empty when the file was read. The draft is then valid.
    std::string   error;
    SavedMapDraft draft;
};

[[nodiscard]] std::string                               EncodeMapDraft(const SavedMapDraft& draft);
[[nodiscard]] std::expected<SavedMapDraft, std::string> DecodeMapDraft(std::string_view text);
[[nodiscard]] std::expected<SavedMapDraft, std::string> LoadMapDraft(const std::filesystem::path& path);
// Writes a sibling temporary file, then renames it over path. Creates the parent folder.
[[nodiscard]] std::expected<void, std::string> SaveMapDraft(const std::filesystem::path& path, std::string_view encoded);
// Drafts in folder, most recently saved first. Unreadable files carry an error.
[[nodiscard]] std::vector<MapDraftListing> ListMapDrafts(const std::filesystem::path& folder);
// Each listed file that exists, in order. One that cannot be checked or read carries an error.
[[nodiscard]] std::vector<MapDraftListing> ListMapFiles(std::span<const std::filesystem::path> files);
// A file name in folder that no draft uses yet, or empty when the folder cannot be checked.
[[nodiscard]] std::filesystem::path NewMapDraftPath(const std::filesystem::path& folder, std::int64_t now);
// The Tools data folder for the user, or empty when the platform gives no home.
[[nodiscard]] std::filesystem::path DefaultAppDataFolder();
[[nodiscard]] std::filesystem::path MapRecoveryFolder(const std::filesystem::path& dataFolder);
[[nodiscard]] std::filesystem::path RecentMapsFile(const std::filesystem::path& dataFolder);
// Paths in the recent list, newest first. A missing or unreadable list is empty.
[[nodiscard]] std::vector<std::filesystem::path> LoadRecentMaps(const std::filesystem::path& file);
[[nodiscard]] std::expected<void, std::string>   SaveRecentMaps(const std::filesystem::path& file, std::span<const std::filesystem::path> maps);
void                                             RememberRecentMap(std::vector<std::filesystem::path>& recent, const std::filesystem::path& file);
// A file name for the map: its name with characters Windows refuses replaced.
[[nodiscard]] std::filesystem::path MapFileName(std::string_view identity);
[[nodiscard]] std::int64_t          MapDraftClockNow();
// "just now", "5 minutes ago", "yesterday", ...
[[nodiscard]] std::string FormatMapDraftAge(std::int64_t savedAt, std::int64_t now);

} // namespace rerevved::tools
