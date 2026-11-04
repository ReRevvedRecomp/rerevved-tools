#pragma once

#include "map_document.h"
#include "scenario_document.h"
#include "terrain_dds.h"
#include "texture_import.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rerevved::tools
{

enum class MapStep
{
    Start,
    EditGrid,
    Heights,
    MountainHillBlends,
    Lightmap,
    Scenario,
    Review,
    Export,
};

enum class CreatorPaneState
{
    NoDraft,
    Working,
    Error,
    Partial,
    Success,
    ResearchBlocked,
};

enum class CreatorPrerequisite
{
    MapDraft,
    CurrentRevision,
    DiscardConfirmation,
    ValidStep,
    ValidTile,
};

[[nodiscard]] std::string_view CreatorPrerequisiteMessage(CreatorPrerequisite prerequisite);

struct CreatorTransitionFailure
{
    std::vector<CreatorPrerequisite> missing;
};

using CreatorTransition = std::expected<void, CreatorTransitionFailure>;

struct InspectionSelection
{
    std::uint64_t              documentId = 0;
    std::optional<std::size_t> entryIndex;
    std::uint64_t              openAttempt = 0;

    [[nodiscard]] friend bool operator==(const InspectionSelection&, const InspectionSelection&) = default;
};

enum class MapArtifact : std::size_t
{
    Map,
    Scenario,
    Heights,
    MountainHillBlends,
    Lightmap,
    Fpk,
    Count,
};

inline constexpr std::size_t kMapArtifactCount = static_cast<std::size_t>(MapArtifact::Count);

struct MapArtifactReadiness
{
    bool        ready = false;
    std::string diagnostic;
};

struct CreatorReadinessCount
{
    std::size_t ready    = 0;
    std::size_t required = kMapArtifactCount;
};

struct MapHeightsSource
{
    std::vector<std::uint16_t>                         samples;
    bool                                               readBackPassed = false;
    std::optional<std::array<std::byte, kMapCoreSize>> generatedFrom;
};

struct MapTerrainSource
{
    std::vector<std::byte>                             dds;
    bool                                               readBackPassed = false;
    std::optional<std::array<std::byte, kMapCoreSize>> generatedFrom;
};

inline constexpr std::size_t kMapTerrainCount = 2;

struct MapDraft
{
    MapStep                         step     = MapStep::Start;
    std::uint64_t                   revision = 0;
    bool                            edited   = false;
    bool                            started  = false;
    std::string                     identity;
    std::optional<std::size_t>      selectedTile;
    std::optional<ScenarioDocument> scenario;
    std::optional<MapHeightsSource> heights;
    // Indexed by TerrainTexture.
    std::array<std::optional<MapTerrainSource>, kMapTerrainCount> terrain;
    CreatorReadinessCount                                         readiness;
    std::array<MapArtifactReadiness, kMapArtifactCount>           artifacts;
};

struct CreatorDiscardRequest
{
    std::uint64_t revision = 0;
};

// Session state owns immutable import results. Inspection retains identity only.
class CreatorWorkspace
{
public:
    [[nodiscard]] const MapDraft&  Map() const noexcept;
    [[nodiscard]] CreatorPaneState MapReadinessState() const noexcept;

    [[nodiscard]] bool                                      InspectionVisible() const noexcept;
    void                                                    SetInspectionVisible(bool visible) noexcept;
    [[nodiscard]] const std::optional<InspectionSelection>& Inspection() const noexcept;
    void                                                    SetInspection(std::optional<InspectionSelection> selection) noexcept;

    [[nodiscard]] CreatorTransition SelectMapStep(MapStep step);

    [[nodiscard]] CreatorTransition StartMapDraft(std::string identity);
    [[nodiscard]] CreatorTransition ReviseMap();
    [[nodiscard]] CreatorTransition SetMapScenario(std::uint64_t revision, ScenarioDocument scenario);
    [[nodiscard]] CreatorTransition SetMapHeights(std::uint64_t revision, std::optional<MapHeightsSource> heights);
    [[nodiscard]] CreatorTransition SetMapTerrain(std::uint64_t revision, TerrainTexture role, std::optional<MapTerrainSource> source);
    [[nodiscard]] CreatorTransition SelectMapTile(std::optional<std::size_t> tile);
    [[nodiscard]] CreatorTransition RecordMapReadiness(std::uint64_t                                       revision,
                                                       std::array<MapArtifactReadiness, kMapArtifactCount> artifacts);

    void                                                      RequestDiscard() noexcept;
    void                                                      CancelDiscard() noexcept;
    [[nodiscard]] const std::optional<CreatorDiscardRequest>& PendingDiscard() const noexcept;
    [[nodiscard]] CreatorTransition                           ConfirmDiscard();

private:
    MapDraft                             map;
    bool                                 inspectionVisible = false;
    std::optional<InspectionSelection>   inspection;
    std::optional<CreatorDiscardRequest> discard;
};

} // namespace rerevved::tools
