#include "map/creator_workspace.h"

#include <utility>

namespace rerevved::tools
{
namespace
{

CreatorTransition reject(CreatorPrerequisite prerequisite)
{
    return std::unexpected(CreatorTransitionFailure{ { prerequisite } });
}

} // namespace

std::string_view CreatorPrerequisiteMessage(CreatorPrerequisite prerequisite)
{
    switch (prerequisite)
    {
        case CreatorPrerequisite::MapDraft:
            return "Start a new 32x32 draft or explicitly import a 1088 byte MAP.";
        case CreatorPrerequisite::CurrentRevision:
            return "Repeat the operation for the current draft revision.";
        case CreatorPrerequisite::DiscardConfirmation:
            return "Request discard and explicitly confirm the named draft revision.";
        case CreatorPrerequisite::ValidStep:
            return "Choose a listed journey step.";
        case CreatorPrerequisite::ValidTile:
            return "Choose a file order tile offset from 0 through 1023.";
    }
    return "Unknown prerequisite.";
}

const MapDraft& CreatorWorkspace::Map() const noexcept
{
    return map;
}

CreatorPaneState CreatorWorkspace::MapReadinessState() const noexcept
{
    if (!map.started)
    {
        return CreatorPaneState::NoDraft;
    }
    if (map.readiness.ready == 0)
    {
        return CreatorPaneState::ResearchBlocked;
    }
    return map.readiness.ready == map.readiness.required ? CreatorPaneState::Success : CreatorPaneState::Partial;
}

bool CreatorWorkspace::InspectionVisible() const noexcept
{
    return inspectionVisible;
}

void CreatorWorkspace::SetInspectionVisible(bool visible) noexcept
{
    inspectionVisible = visible;
}

const std::optional<InspectionSelection>& CreatorWorkspace::Inspection() const noexcept
{
    return inspection;
}

void CreatorWorkspace::SetInspection(std::optional<InspectionSelection> selection) noexcept
{
    inspection = selection;
}

CreatorTransition CreatorWorkspace::SelectMapStep(MapStep step)
{
    switch (step)
    {
        case MapStep::Start:
            break;
        case MapStep::EditGrid:
        case MapStep::Heights:
        case MapStep::MountainHillBlends:
        case MapStep::Lightmap:
        case MapStep::Scenario:
        case MapStep::Review:
        case MapStep::Export:
            if (!map.started)
            {
                return reject(CreatorPrerequisite::MapDraft);
            }
            break;
        default:
            return reject(CreatorPrerequisite::ValidStep);
    }
    map.step = step;
    return {};
}

CreatorTransition CreatorWorkspace::StartMapDraft(std::string identity)
{
    if (map.started)
    {
        return reject(CreatorPrerequisite::DiscardConfirmation);
    }
    ++map.revision;
    map.started  = true;
    map.edited   = true;
    map.identity = std::move(identity);
    return {};
}

CreatorTransition CreatorWorkspace::ReviseMap()
{
    if (!map.started)
    {
        return reject(CreatorPrerequisite::MapDraft);
    }
    ++map.revision;
    map.edited          = true;
    map.artifacts       = {};
    map.readiness.ready = 0;
    map.step            = MapStep::EditGrid;
    return {};
}

CreatorTransition CreatorWorkspace::SetMapScenario(std::uint64_t revision, ScenarioDocument scenario)
{
    if (revision != map.revision)
    {
        return reject(CreatorPrerequisite::CurrentRevision);
    }
    if (!map.started)
    {
        return reject(CreatorPrerequisite::MapDraft);
    }
    if (map.scenario && *map.scenario == scenario)
    {
        return {};
    }
    const auto step = map.step;
    static_cast<void>(ReviseMap());
    map.step = step;
    // Keep invalid edits so every model diagnostic can be shown without repairing input.
    map.scenario = std::move(scenario);
    return {};
}

CreatorTransition CreatorWorkspace::SetMapHeights(std::uint64_t revision, std::optional<MapHeightsSource> heights)
{
    if (revision != map.revision)
    {
        return reject(CreatorPrerequisite::CurrentRevision);
    }
    if (!map.started)
    {
        return reject(CreatorPrerequisite::MapDraft);
    }
    const auto step = map.step;
    static_cast<void>(ReviseMap());
    map.step    = step;
    map.heights = std::move(heights);
    return {};
}

CreatorTransition CreatorWorkspace::SetMapTerrain(std::uint64_t revision, TerrainTexture role, std::optional<MapTerrainSource> source)
{
    if (revision != map.revision)
    {
        return reject(CreatorPrerequisite::CurrentRevision);
    }
    if (!map.started)
    {
        return reject(CreatorPrerequisite::MapDraft);
    }
    const auto step = map.step;
    static_cast<void>(ReviseMap());
    map.step                                    = step;
    map.terrain[static_cast<std::size_t>(role)] = std::move(source);
    return {};
}

CreatorTransition CreatorWorkspace::SelectMapTile(std::optional<std::size_t> tile)
{
    if (!map.started)
    {
        return reject(CreatorPrerequisite::MapDraft);
    }
    if (tile && *tile >= 32 * 32)
    {
        return reject(CreatorPrerequisite::ValidTile);
    }
    map.selectedTile = tile;
    return {};
}

CreatorTransition CreatorWorkspace::RecordMapReadiness(std::uint64_t                                       revision,
                                                       std::array<MapArtifactReadiness, kMapArtifactCount> artifacts)
{
    if (revision != map.revision)
    {
        return reject(CreatorPrerequisite::CurrentRevision);
    }
    if (!map.started)
    {
        return reject(CreatorPrerequisite::MapDraft);
    }
    map.artifacts       = std::move(artifacts);
    map.readiness.ready = 0;
    for (const auto& artifact : map.artifacts)
    {
        if (artifact.ready)
        {
            ++map.readiness.ready;
        }
    }
    return {};
}

void CreatorWorkspace::RequestDiscard() noexcept
{
    discard = CreatorDiscardRequest{ map.revision };
}

void CreatorWorkspace::CancelDiscard() noexcept
{
    discard.reset();
}

const std::optional<CreatorDiscardRequest>& CreatorWorkspace::PendingDiscard() const noexcept
{
    return discard;
}

CreatorTransition CreatorWorkspace::ConfirmDiscard()
{
    if (!discard)
    {
        return reject(CreatorPrerequisite::DiscardConfirmation);
    }
    if (discard->revision != map.revision)
    {
        return reject(CreatorPrerequisite::CurrentRevision);
    }
    const auto revision = map.revision;
    map                 = {};
    map.revision        = revision + 1;
    discard.reset();
    return {};
}

} // namespace rerevved::tools
