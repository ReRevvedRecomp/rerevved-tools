#pragma once

#include "map/creator_workspace.h"
#include "map_document.h"
#include "scenario_document.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rerevved::tools
{

struct ScenarioFormReview
{
    std::vector<ScenarioIssue>   issues;
    std::optional<ScenarioIssue> writerIssue;
    std::vector<std::byte>       xml;
    bool                         readBackPassed = false;
};

// These stages describe independently verified artifacts, never UI warning overrides.
struct MapRoleReadiness
{
    std::string                provenance = "No authored source supplied.";
    MapArtifactReadiness       source{ false, "An authored source and redistribution provenance are required." };
    MapArtifactReadiness       contract;
    MapArtifactReadiness       encoding{ false, "No role specific Xbox DDS encoding has passed." };
    MapArtifactReadiness       readBack{ false, "No role specific DDS read back has passed." };
    std::array<std::string, 7> details{ "Unproved", "Unproved", "Unproved", "Unproved", "Unproved", "Unproved", "Unproved" };
};

struct MapReadinessInputs
{
    std::uint64_t                   revision = 0;
    MapArtifactReadiness            map{ false, "Open or create the tile grid." };
    std::array<MapRoleReadiness, 3> roles;
};

struct MapReadinessReview
{
    std::array<MapArtifactReadiness, kMapArtifactCount> artifacts;
    CreatorReadinessCount                               count;
    bool                                                exportEnabled = false;
};

struct ScenarioEditorState
{
    MapReadinessInputs readiness;
    std::string        diagnostic;
};

[[nodiscard]] std::string        FormatScenarioIssue(const ScenarioIssue& issue);
[[nodiscard]] ScenarioFormReview ReviewScenarioForm(const ScenarioDocument& document);
// Rules beyond ValidateScenarioDocument: one shared MAP letter and IDs not used by the game.
[[nodiscard]] std::vector<std::string> ScenarioMapBindingBlockers(const ScenarioDocument& document);
// First title and description written to every language.
void                             SingleMapScenario(ScenarioDocument& document);
[[nodiscard]] CreatorTransition  ApplyScenarioForm(CreatorWorkspace& workspace, std::uint64_t revision, ScenarioDocument document);
[[nodiscard]] MapReadinessReview ReviewMapReadiness(const MapDraft& draft, const MapReadinessInputs& inputs);

// Revisions invalidate supplied artifact proofs. The scenario remains owned by the draft.
// The MAP input follows the supplied document on every call.
void RefreshMapReadiness(CreatorWorkspace& workspace, ScenarioEditorState& state, const MapDocument* map);

} // namespace rerevved::tools
