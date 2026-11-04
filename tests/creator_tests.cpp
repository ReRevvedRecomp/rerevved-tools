#include "map/creator_workspace.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

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

void expectPass(const CreatorTransition& result, std::string_view message)
{
    expect(result.has_value(), message);
}

void expectMissing(const CreatorTransition& result, CreatorPrerequisite prerequisite, std::string_view message)
{
    expect(!result, message);
    if (result)
    {
        return;
    }
    expect(std::ranges::find(result.error().missing, prerequisite) != result.error().missing.end(), message);
    expect(!CreatorPrerequisiteMessage(prerequisite).empty(), "a blocked action names its prerequisite");
}

void testStartupState()
{
    const CreatorWorkspace workspace;
    expect(!workspace.InspectionVisible(), "startup collapses inspection");
    expect(!workspace.Inspection(), "startup has no inspection reference");
    expect(workspace.Map().step == MapStep::Start && !workspace.Map().started && !workspace.Map().edited, "map starts at M1 without a draft");
    expect(workspace.Map().readiness.ready == 0 && workspace.Map().readiness.required == 6, "map readiness count is available before starting");
    expect(!workspace.PendingDiscard(), "startup has no discard prompt");
    expect(workspace.MapReadinessState() == CreatorPaneState::NoDraft, "an empty map reflects its owner");
}

void testInvalidTransitionsNamePrerequisites()
{
    CreatorWorkspace workspace;
    for (const auto step : { MapStep::EditGrid, MapStep::Scenario, MapStep::Review, MapStep::Export })
    {
        expectMissing(workspace.SelectMapStep(step), CreatorPrerequisite::MapDraft, "map advance without a draft is blocked");
        expect(workspace.Map().step == MapStep::Start, "invalid map transition preserves its step");
    }
    expectMissing(workspace.SelectMapStep(static_cast<MapStep>(99)), CreatorPrerequisite::ValidStep, "unknown map step is rejected");
}

void testMapReadinessIsRevisionBound()
{
    CreatorWorkspace workspace;
    expectMissing(workspace.ReviseMap(), CreatorPrerequisite::MapDraft, "map edit requires a draft");
    expectPass(workspace.StartMapDraft("seed"), "start map for readiness");
    expect(workspace.MapReadinessState() == CreatorPaneState::ResearchBlocked, "map blockers never appear as Loading");
    expectPass(workspace.SelectMapStep(MapStep::EditGrid), "M2 opens for a map");
    expectPass(workspace.SelectMapStep(MapStep::Scenario), "M3 opens for the same map");
    expectPass(workspace.SelectMapStep(MapStep::Review), "M4 opens for the same map");
    std::array<MapArtifactReadiness, kMapArtifactCount> artifacts;
    for (auto& artifact : artifacts)
    {
        artifact.ready = true;
    }
    artifacts[static_cast<std::size_t>(MapArtifact::Lightmap)] = { false, "Unproved Xbox tiling" };
    const auto revision                                        = workspace.Map().revision;
    expectPass(workspace.RecordMapReadiness(revision, artifacts), "attach partial readiness");
    expect(workspace.Map().readiness.ready == 5 && workspace.Map().readiness.required == 6, "compact readiness reflects five of six artifacts");
    expect(workspace.MapReadinessState() == CreatorPaneState::Partial, "mixed readiness is Partial");
    expect(workspace.Map().artifacts[static_cast<std::size_t>(MapArtifact::Lightmap)].diagnostic == "Unproved Xbox tiling", "role retains its exact blocker");
    artifacts[static_cast<std::size_t>(MapArtifact::Lightmap)].ready = true;
    expectPass(workspace.RecordMapReadiness(revision, artifacts), "attach synthetic complete readiness");
    expect(workspace.MapReadinessState() == CreatorPaneState::Success, "complete synthetic readiness is Success");
    expectPass(workspace.ReviseMap(), "edit invalidates map readiness");
    expect(workspace.Map().readiness.ready == 0, "a changed map has no current ready artifacts");
    expectMissing(workspace.RecordMapReadiness(revision, artifacts), CreatorPrerequisite::CurrentRevision, "stale map readiness cannot attach");
    expectPass(workspace.SelectMapTile(1023), "last fixed-profile tile is selectable");
    expectMissing(workspace.SelectMapTile(1024), CreatorPrerequisite::ValidTile, "out-of-profile selection is rejected");
    expect(workspace.Map().selectedTile == 1023, "invalid selection preserves the prior tile");
}

void testDiscardRequiresConfirmation()
{
    CreatorWorkspace workspace;
    expectPass(workspace.StartMapDraft("keep map"), "start map before discard");
    expectPass(workspace.SelectMapStep(MapStep::Scenario), "retain map step");
    expectPass(workspace.SelectMapTile(71), "retain map selection");
    const InspectionSelection reference{ 42, 7, 3 };
    workspace.SetInspection(reference);
    workspace.SetInspectionVisible(true);
    const auto mapRevision = workspace.Map().revision;
    expectMissing(workspace.ConfirmDiscard(), CreatorPrerequisite::DiscardConfirmation, "direct discard is refused");
    expectMissing(workspace.StartMapDraft("replacement"), CreatorPrerequisite::DiscardConfirmation, "new map cannot replace an existing draft");
    workspace.RequestDiscard();
    expect(workspace.Map().started && workspace.Map().revision == mapRevision, "request alone does not discard");
    expect(workspace.PendingDiscard() && workspace.PendingDiscard()->revision == mapRevision, "prompt names the draft revision");
    workspace.CancelDiscard();
    expect(!workspace.PendingDiscard() && workspace.Map().started, "Return preserves the draft");
    workspace.RequestDiscard();
    expectPass(workspace.ConfirmDiscard(), "confirm map discard");
    expect(!workspace.Map().started && workspace.Map().identity.empty() && !workspace.Map().selectedTile && workspace.Map().readiness.ready == 0, "map discard clears map identity, selection and readiness");
    expect(workspace.Map().step == MapStep::Start && workspace.Map().revision > mapRevision, "map discard resets step without reusing old revisions");
    expect(workspace.Inspection() == reference && workspace.InspectionVisible(), "map discard preserves reference identity and visibility");

    expectPass(workspace.StartMapDraft("new map"), "new map starts after confirmed discard");
    workspace.RequestDiscard();
    expectPass(workspace.ReviseMap(), "edit map after discard prompt");
    expectMissing(workspace.ConfirmDiscard(), CreatorPrerequisite::CurrentRevision, "map confirmation captures a specific revision");
    expect(workspace.Map().started, "stale map confirmation preserves new edits");
}

void testInspectionIsIndependent()
{
    CreatorWorkspace workspace;
    expectPass(workspace.StartMapDraft("independent map"), "start map while inspecting");
    expectPass(workspace.SelectMapStep(MapStep::Review), "retain map review while inspecting");
    const auto                mapRevision = workspace.Map().revision;
    const InspectionSelection first{ 10, 2, 1 };
    const InspectionSelection second{ 20, 9, 4 };
    workspace.SetInspection(first);
    workspace.SetInspectionVisible(true);
    expect(workspace.Inspection() == first && workspace.InspectionVisible(), "reference and visibility are held independently");
    workspace.SetInspectionVisible(false);
    expect(workspace.Inspection() == first, "collapse retains reference selection");
    workspace.SetInspectionVisible(true);
    workspace.SetInspection(second);
    expect(workspace.Map().revision == mapRevision && workspace.Map().step == MapStep::Review, "reference selection does not replace map state");
    workspace.SetInspection(std::nullopt);
    expect(workspace.Map().started, "closing reference preserves the authoring draft");
}

} // namespace

int main()
{
    testStartupState();
    testInvalidTransitionsNamePrerequisites();
    testMapReadinessIsRevisionBound();
    testDiscardRequiresConfirmation();
    testInspectionIsIndependent();
    if (failures != 0)
    {
        std::cerr << failures << " creator test(s) failed\n";
        return 1;
    }
    std::cout << "creator tests passed\n";
    return 0;
}
