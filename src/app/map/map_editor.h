#pragma once

#include "map/creator_workspace.h"
#include "map/map_draft_store.h"
#include "map/map_edit_model.h"
#include "map/map_wizard.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace rerevved::tools
{

struct MapPreviewKey
{
    LandSize      size;
    std::uint32_t seed;
    MapMirror     mirror;
    int           players;

    [[nodiscard]] friend bool operator==(const MapPreviewKey&, const MapPreviewKey&) = default;
};

struct MapEditorState
{
    MapEditModel     model;
    MapBrushSettings brush;
    std::string      diagnostic;
    bool             focusTile     = false;
    bool             focusGrid     = false;
    bool             fit           = false;
    int              lastDrawFrame = -1;
    // Advisory checks for the draft, recomputed when its tile bytes change.
    std::vector<MapTileWarning>                        warnings;
    std::optional<std::array<std::byte, kMapCoreSize>> warningsCore;
    // The start page: Create or Recent, picked on the shell's tab row.
    int                                                             startTab      = 0;
    bool                                                            requestCreate = false;
    bool                                                            canCreate     = false;
    std::string                                                     createBlocker;
    std::string                                                     createProblem;
    std::array<char, 96>                                            mapName{};
    StartingLand                                                    land       = StartingLand::Pangaea;
    LandSize                                                        landSize   = LandSize::Medium;
    std::uint32_t                                                   layoutSeed = kFirstLayoutSeed;
    MapMirror                                                       mirror     = MapMirror::Off;
    int                                                             players    = kStartingLandPlayers;
    int                                                             landGroup  = 0;
    std::array<MapDocument, kStartingLands.size()>                  previews{};
    std::array<std::optional<MapPreviewKey>, kStartingLands.size()> previewKeys;
    std::uint32_t                                                   terrainSeed = 1000;
    // The suggestion for the typed name. Reset when the usage it read changes.
    std::optional<std::string> identityName;
    MapIdentity                identity;
};

struct MapStartChoice
{
    std::optional<std::filesystem::path> open;    // a recent map file to open
    std::optional<MapIdentity>           started; // the names for a map started on the page
};

[[nodiscard]] MapStartChoice DrawMapStart(CreatorWorkspace& workspace, MapEditorState& state, std::span<const MapDraftListing> recent, const MapPackUsage& usage);
void                         DrawMapGrid(CreatorWorkspace& workspace, MapEditorState& state);
void                         DrawMapTileInspector(MapEditorState& state);
void                         UndoMapEdit(CreatorWorkspace& workspace, MapEditorState& state);
void                         RedoMapEdit(CreatorWorkspace& workspace, MapEditorState& state);

inline void CancelMapEditorStroke(CreatorWorkspace& workspace, MapEditorState& state)
{
    if (state.model.CancelStroke())
    {
        const auto step = workspace.Map().step;
        static_cast<void>(workspace.ReviseMap());
        static_cast<void>(workspace.SelectMapStep(step));
    }
}

} // namespace rerevved::tools
