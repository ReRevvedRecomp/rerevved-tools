#include "map/scenario_form.h"

#include "map/map_pack_export.h"

#include <algorithm>
#include <utility>

namespace rerevved::tools
{
namespace
{

constexpr std::string_view kContractTrial =
    "Matches the layout the game loaded in a map pack trial.";

void block(MapArtifactReadiness& artifact, std::string_view diagnostic)
{
    artifact.ready = false;
    if (!artifact.diagnostic.empty())
    {
        artifact.diagnostic += '\n';
    }
    artifact.diagnostic += diagnostic;
}

void requireStage(MapArtifactReadiness& artifact, std::string_view stage, const MapArtifactReadiness& check)
{
    if (!check.ready)
    {
        block(artifact, std::string(stage) + ": " + (check.diagnostic.empty() ? "Not validated." : check.diagnostic));
    }
}

void applyHeights(MapRoleReadiness& progress, const std::optional<MapHeightsSource>& heights)
{
    progress.contract = { true, std::string(kContractTrial) };
    if (!heights)
    {
        progress.source = { false, "The heights image is generated from the grid." };
        return;
    }
    progress.provenance = "Generated from the grid.";
    progress.source     = { true, progress.provenance };
    progress.encoding   = { heights->readBackPassed,
                            heights->readBackPassed
                                ? "Encoded in the shipped 512 x 512 16 bit layout."
                                : "Heights DDS encoding failed." };
    progress.readBack   = { heights->readBackPassed,
                            heights->readBackPassed
                                ? "Every sample read back exactly."
                                : "Heights DDS read back did not match the samples." };
}

void applyTerrain(MapRoleReadiness& progress, TerrainTexture role, const std::optional<MapTerrainSource>& source)
{
    const bool blends = role == TerrainTexture::MountainHillBlends;
    progress.contract = { true, std::string(kContractTrial) };
    if (!source)
    {
        progress.source = { false, blends ? "The blends image is generated from the grid." : "The lightmap is generated from the grid." };
        return;
    }
    progress.provenance = "Generated from the grid.";
    progress.source     = { true, progress.provenance };
    progress.encoding   = { source->readBackPassed,
                            source->readBackPassed
                                ? "Encoded as one DXT1 level in the shipped layout."
                                : "DXT1 encoding failed." };
    progress.readBack   = { source->readBackPassed,
                            source->readBackPassed ? "Header and blocks read back and decoded."
                                                   : "DDS read back did not pass." };
}

} // namespace

std::string FormatScenarioIssue(const ScenarioIssue& issue)
{
    std::string result;
    if (issue.entryIndex != kScenarioNoEntry)
    {
        result = "Entry " + std::to_string(issue.entryIndex) + " / ";
    }
    if (!issue.field.empty())
    {
        result += issue.field + ": ";
    }
    result += ScenarioErrorMessage(issue.code);
    return result;
}

std::vector<std::string> ScenarioMapBindingBlockers(const ScenarioDocument& document)
{
    std::vector<std::string> blockers;
    if (document.entries.empty())
    {
        return blockers;
    }
    const auto& first = document.entries.front().map;
    if (first == "NONE" || std::ranges::any_of(document.entries, [&](const ScenarioEntry& entry)
                                               {
                                                   return entry.map != first;
                                               }))
    {
        blockers.emplace_back("Map Letter: choose one letter, A to Z.");
    }
    if (std::ranges::any_of(document.entries, [](const ScenarioEntry& entry)
                            {
                                return entry.id < kMapPackFirstScenarioId;
                            }))
    {
        blockers.emplace_back("Scenario ID: 100 or higher. Lower IDs belong to the game.");
    }
    return blockers;
}

ScenarioFormReview ReviewScenarioForm(const ScenarioDocument& document)
{
    ScenarioFormReview review;
    review.issues = ValidateScenarioDocument(document);
    if (!review.issues.empty())
    {
        return review;
    }
    auto encoded = EncodeScenarioXml(document);
    if (!encoded)
    {
        review.writerIssue = encoded.error();
        return review;
    }
    const auto parsed = ParseScenarioDocument(*encoded);
    if (!parsed)
    {
        review.writerIssue = parsed.error();
        return review;
    }
    if (*parsed != document)
    {
        review.writerIssue = ScenarioIssue{ ScenarioError::ReadBackFailed, kScenarioNoEntry, "XML" };
        return review;
    }
    review.xml            = std::move(*encoded);
    review.readBackPassed = true;
    return review;
}

CreatorTransition ApplyScenarioForm(CreatorWorkspace& workspace, std::uint64_t revision, ScenarioDocument document)
{
    return workspace.SetMapScenario(revision, std::move(document));
}

MapReadinessReview ReviewMapReadiness(const MapDraft& draft, const MapReadinessInputs& inputs)
{
    MapReadinessReview review;
    review.artifacts[static_cast<std::size_t>(MapArtifact::Map)] = inputs.map;
    auto& scenario                                               = review.artifacts[static_cast<std::size_t>(MapArtifact::Scenario)];
    scenario.ready                                               = true;
    if (!draft.scenario || draft.scenario->entries.empty())
    {
        block(scenario, "Create at least one scenario entry for this bundle.");
    }
    if (draft.scenario)
    {
        const auto form = ReviewScenarioForm(*draft.scenario);
        for (const auto& issue : form.issues)
        {
            block(scenario, FormatScenarioIssue(issue));
        }
        if (form.writerIssue)
        {
            block(scenario, FormatScenarioIssue(*form.writerIssue));
        }
        if (!form.readBackPassed && form.issues.empty() && !form.writerIssue)
        {
            block(scenario, "Scenario XML read back has not passed.");
        }
    }
    if (draft.scenario)
    {
        for (const auto& blocker : ScenarioMapBindingBlockers(*draft.scenario))
        {
            block(scenario, blocker);
        }
    }
    if (scenario.ready)
    {
        scenario.diagnostic = "Scenario XML encoded and read back.";
    }
    for (std::size_t role = 0; role < inputs.roles.size(); ++role)
    {
        auto&       artifact = review.artifacts[static_cast<std::size_t>(MapArtifact::Heights) + role];
        const auto& progress = inputs.roles[role];
        artifact.ready       = true;
        requireStage(artifact, "Source/provenance", progress.source);
        requireStage(artifact, "Contract", progress.contract);
        requireStage(artifact, "Encode", progress.encoding);
        requireStage(artifact, "Read back", progress.readBack);
        if (artifact.ready)
        {
            artifact.diagnostic = "Source, encoding and read back passed.";
        }
    }
    const auto isReady = [&](MapArtifact artifact)
    {
        return review.artifacts[static_cast<std::size_t>(artifact)].ready;
    };
    auto& fpk      = review.artifacts[static_cast<std::size_t>(MapArtifact::Fpk)];
    fpk.ready      = isReady(MapArtifact::Map) && isReady(MapArtifact::Scenario) && isReady(MapArtifact::Heights) && isReady(MapArtifact::MountainHillBlends) && isReady(MapArtifact::Lightmap);
    fpk.diagnostic = fpk.ready ? "Packed into dlc.fpk at export." : "Waits on the files above.";
    for (auto& artifact : review.artifacts)
    {
        if (!draft.started)
        {
            block(artifact, CreatorPrerequisiteMessage(CreatorPrerequisite::MapDraft));
        }
        if (inputs.revision != draft.revision)
        {
            block(artifact, CreatorPrerequisiteMessage(CreatorPrerequisite::CurrentRevision));
        }
        if (!artifact.ready && artifact.diagnostic.empty())
        {
            artifact.diagnostic = "Artifact preparation and read back have not passed.";
        }
        if (artifact.ready)
        {
            ++review.count.ready;
        }
    }
    review.exportEnabled = draft.started && review.count.ready == review.count.required;
    return review;
}

void RefreshMapReadiness(CreatorWorkspace& workspace, ScenarioEditorState& state, const MapDocument* map)
{
    const auto& draft = workspace.Map();
    if (state.readiness.revision != draft.revision)
    {
        state.readiness          = {};
        state.readiness.revision = draft.revision;
        state.diagnostic.clear();
        applyHeights(state.readiness.roles[0], draft.heights);
        applyTerrain(state.readiness.roles[1], TerrainTexture::MountainHillBlends, draft.terrain[0]);
        applyTerrain(state.readiness.roles[2], TerrainTexture::Lightmap, draft.terrain[1]);
    }
    state.readiness.map = map ? MapArtifactReadiness{ true, "32 x 32 tile grid ready." } : MapArtifactReadiness{ false, "Open or create the tile grid." };
    if (draft.started)
    {
        auto review = ReviewMapReadiness(draft, state.readiness);
        static_cast<void>(workspace.RecordMapReadiness(draft.revision, std::move(review.artifacts)));
    }
}

void SingleMapScenario(ScenarioDocument& document)
{
    if (document.entries.empty())
    {
        document.entries.emplace_back();
    }
    document.entries.resize(1);
    auto&      entry = document.entries.front();
    const auto first = [](const ScenarioLiterals& texts)
    {
        const auto found = std::ranges::find_if(texts, [](const std::string& text)
                                                {
                                                    return !text.empty();
                                                });
        return found != texts.end() ? *found : std::string();
    };
    entry.title.fill(first(entry.title));
    entry.description.fill(first(entry.description));
}

} // namespace rerevved::tools
