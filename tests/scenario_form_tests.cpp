#include "map/scenario_form.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>

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

bool missing(const CreatorTransition& result, CreatorPrerequisite prerequisite)
{
    return !result && std::ranges::find(result.error().missing, prerequisite) != result.error().missing.end();
}

ScenarioDocument editedDocument()
{
    ScenarioEntry entry;
    entry.map         = "A";
    entry.id          = std::numeric_limits<std::int32_t>::min();
    entry.multiplayer = true;
    entry.title       = { "Synthetic title", "Titre", "Titolo", "Titel", "Titulo" };
    entry.description = { "Signed form edits", "Description", "Descrizione", "Beschreibung", "Descripcion" };
    for (const auto& descriptor : ScenarioVariators())
    {
        entry.variators[descriptor.slot] = descriptor.slot % 2 == 0 ? std::numeric_limits<std::int16_t>::min() : std::numeric_limits<std::int16_t>::max();
    }
    entry.variators[30] = -1;
    return { { entry } };
}

ScenarioDocument readyDocument()
{
    auto document          = editedDocument();
    document.entries[0].id = 100;
    return document;
}

MapReadinessInputs syntheticProofs(std::uint64_t revision)
{
    MapReadinessInputs inputs;
    inputs.revision = revision;
    inputs.map      = { true, "Synthetic MAP ready." };
    for (auto& role : inputs.roles)
    {
        role.provenance = "Synthetic authored source";
        role.source     = { true, "Synthetic source/provenance proof." };
        role.contract   = { true, "Synthetic role contract proof." };
        role.encoding   = { true, "Synthetic Xbox encoding proof." };
        role.readBack   = { true, "Synthetic DDS read-back proof." };
        role.details.fill("Synthetic proof only");
    }
    return inputs;
}

void testSignedFormWriterRoundTrip()
{
    static_assert(std::is_same_v<decltype(ScenarioEntry::id), std::int32_t>);
    static_assert(std::is_same_v<decltype(ScenarioEntry::variators)::value_type, std::int16_t>);
    CreatorWorkspace workspace;
    expect(workspace.StartMapDraft("synthetic signed form").has_value(), "start map for form round-trip");
    expect(workspace.SelectMapStep(MapStep::Scenario).has_value(), "enter M3");
    ScenarioDocument defaults{ { ScenarioEntry{} } };
    expect(defaults.entries.front().variators == DefaultScenarioVariators() && defaults.entries.front().variators[30] == -1, "form begins with the real signed Xbox defaults");
    expect(ApplyScenarioForm(workspace, workspace.Map().revision, defaults).has_value(), "create the real scenario model");
    auto edited = editedDocument();
    expect(ApplyScenarioForm(workspace, workspace.Map().revision, edited).has_value(), "apply edits to every supported field group");
    expect(workspace.Map().step == MapStep::Scenario && workspace.Map().scenario == edited, "form edits retain the M3 step and every exact stored value");
    const auto review = ReviewScenarioForm(*workspace.Map().scenario);
    expect(review.issues.empty() && !review.writerIssue && review.readBackPassed && !review.xml.empty(), "form calls the real writer and strict parser in memory");
    const auto parsed = ParseScenarioDocument(review.xml);
    expect(parsed && *parsed == edited, "real scenario writer round-trip preserves ID, all locales, map, multiplayer and every named signed variator");
    if (parsed)
    {
        const auto& entry = parsed->entries.front();
        expect(entry.id == std::numeric_limits<std::int32_t>::min() && entry.variators[0] == std::numeric_limits<std::int16_t>::min() && entry.variators[1] == std::numeric_limits<std::int16_t>::max() && entry.variators[30] == -1, "signed slots stay signed through form edits and canonical serialization");
    }
    const auto revision = workspace.Map().revision;
    expect(ApplyScenarioForm(workspace, revision, edited).has_value() && workspace.Map().revision == revision, "unchanged form input does not manufacture a revision");
    edited.entries.front().description[4].clear();
    expect(ApplyScenarioForm(workspace, revision, edited).has_value(), "empty literal locale remains a supported edit");
    const auto omitted       = ReviewScenarioForm(*workspace.Map().scenario);
    const auto omittedParsed = ParseScenarioDocument(omitted.xml);
    expect(omittedParsed && *omittedParsed == edited, "writer omission of an empty locale reads back to the same form model");
    expect(missing(ApplyScenarioForm(workspace, revision, defaults), CreatorPrerequisite::CurrentRevision), "stale form cannot replace a newer edit");
}

void testEveryModelIssueAndDuplicateDiagnostic()
{
    CreatorWorkspace workspace;
    expect(workspace.StartMapDraft("invalid form").has_value(), "start map for diagnostic review");
    auto document = editedDocument();
    document.entries.push_back(document.entries.front());
    document.entries[0].map            = "A-B";
    document.entries[0].title[0]       = "Bad & literal";
    document.entries[0].description[4] = "Invalid & Spanish description";
    document.entries[0].variators[26]  = -5;
    const auto expected                = ValidateScenarioDocument(document);
    expect(ApplyScenarioForm(workspace, workspace.Map().revision, document).has_value(), "invalid edits remain editable instead of being repaired or dropped");
    const auto review = ReviewScenarioForm(*workspace.Map().scenario);
    expect(expected.size() == 5 && std::ranges::equal(review.issues, expected, [](const ScenarioIssue& lhs, const ScenarioIssue& rhs)
                                                      {
                                                          return lhs.code == rhs.code && lhs.entryIndex == rhs.entryIndex && lhs.field == rhs.field;
                                                      }),
           "form preserves every C3 issue with its original field and entry index");
    const auto duplicate = std::ranges::find(review.issues, ScenarioError::DuplicateId, &ScenarioIssue::code);
    expect(duplicate != review.issues.end(), "duplicate ID is reported by the real C3 validator");
    if (duplicate != review.issues.end())
    {
        expect(duplicate->entryIndex == 1 && duplicate->field == "ID" && FormatScenarioIssue(*duplicate) == "Entry 1 / ID: " + std::string(ScenarioErrorMessage(ScenarioError::DuplicateId)), "duplicate-ID diagnostic is preserved verbatim with a useful field locator");
    }
    expect(review.xml.empty() && !review.readBackPassed && !review.writerIssue, "all validation issues remain visible and invalid XML has no preview");
    expect(workspace.Map().scenario == document, "validation does not normalize signed slots or rename duplicate IDs");
    auto        inputs    = syntheticProofs(workspace.Map().revision);
    const auto  readiness = ReviewMapReadiness(workspace.Map(), inputs);
    const auto& scenario  = readiness.artifacts[static_cast<std::size_t>(MapArtifact::Scenario)];
    for (const auto& issue : expected)
    {
        expect(scenario.diagnostic.find(FormatScenarioIssue(issue)) != std::string::npos, "bundle checklist retains every scenario issue");
    }
    expect(!scenario.ready && !readiness.exportEnabled, "model validation failure blocks export despite synthetic artifact proofs");
}

void testBindingAndOneRoleBlockExport()
{
    CreatorWorkspace workspace;
    expect(workspace.StartMapDraft("partial bundle").has_value(), "start map for partial bundle");
    auto document = readyDocument();
    expect(ApplyScenarioForm(workspace, workspace.Map().revision, document).has_value(), "use valid real scenario XML for readiness");
    auto              inputs       = syntheticProofs(workspace.Map().revision);
    const std::string lightmapGate = "Synthetic lightmap gate.";
    inputs.roles[2].contract       = { false, lightmapGate };
    const auto review              = ReviewMapReadiness(workspace.Map(), inputs);
    expect(review.artifacts.size() == 6 && review.count.ready == 4 && !review.exportEnabled, "one blocked DDS role blocks its card and the FPK card while all six cards remain");
    for (const auto artifact : { MapArtifact::Map, MapArtifact::Scenario, MapArtifact::Heights, MapArtifact::MountainHillBlends })
    {
        expect(review.artifacts[static_cast<std::size_t>(artifact)].ready, "the other ready cards remain present and ready");
    }
    expect(review.artifacts[static_cast<std::size_t>(MapArtifact::Scenario)].diagnostic == "Scenario XML encoded and read back.", "scenario success diagnostic");
    expect(review.artifacts[static_cast<std::size_t>(MapArtifact::Heights)].diagnostic == "Source, encoding and read back passed.", "role success diagnostic");
    expect(review.artifacts[static_cast<std::size_t>(MapArtifact::Lightmap)].diagnostic.find(lightmapGate) != std::string::npos, "blocked DDS role keeps its reason");
    expect(!review.artifacts[static_cast<std::size_t>(MapArtifact::Fpk)].ready && review.artifacts[static_cast<std::size_t>(MapArtifact::Fpk)].diagnostic == "Waits on the files above.", "FPK waits on the roles");
    expect(workspace.RecordMapReadiness(workspace.Map().revision, review.artifacts).has_value(), "record the full independent artifact checklist");
    expect(workspace.Map().readiness.ready == 4 && workspace.MapReadinessState() == CreatorPaneState::Partial, "map draft compact readiness count matches visible cards");
    inputs.roles[2].contract.ready = true;
    const auto complete            = ReviewMapReadiness(workspace.Map(), inputs);
    expect(complete.count.ready == 6 && complete.exportEnabled && complete.artifacts[static_cast<std::size_t>(MapArtifact::Fpk)].diagnostic == "Packed into dlc.fpk at export.", "complete proofs ready every card");

    auto lowId          = document;
    lowId.entries[0].id = 99;
    expect(ApplyScenarioForm(workspace, workspace.Map().revision, lowId).has_value(), "low ID stays editable");
    inputs.revision      = workspace.Map().revision;
    const auto lowReview = ReviewMapReadiness(workspace.Map(), inputs);
    expect(!lowReview.artifacts[static_cast<std::size_t>(MapArtifact::Scenario)].ready && lowReview.artifacts[static_cast<std::size_t>(MapArtifact::Scenario)].diagnostic.find("Scenario ID: 100 or higher. Lower IDs belong to the game.") != std::string::npos && !lowReview.exportEnabled, "IDs below 100 block the scenario");
    auto mixed = readyDocument();
    mixed.entries.push_back(mixed.entries.front());
    mixed.entries[1].id  = 101;
    mixed.entries[1].map = "B";
    expect(ApplyScenarioForm(workspace, workspace.Map().revision, mixed).has_value(), "mixed MAP stays editable");
    inputs.revision        = workspace.Map().revision;
    const auto mixedReview = ReviewMapReadiness(workspace.Map(), inputs);
    expect(mixedReview.artifacts[static_cast<std::size_t>(MapArtifact::Scenario)].diagnostic.find("Map Letter: choose one letter, A to Z.") != std::string::npos, "differing MAP names block the scenario");
    auto none           = readyDocument();
    none.entries[0].map = "NONE";
    expect(ScenarioMapBindingBlockers(none).size() == 1, "NONE is not a pack MAP name");
}

void testResearchDefaultsAndRevisionInvalidation()
{
    CreatorWorkspace    workspace;
    ScenarioEditorState state;
    const auto          empty = ReviewMapReadiness(workspace.Map(), state.readiness);
    expect(empty.count.ready == 0 && empty.artifacts.size() == 6 && !empty.exportEnabled, "no-draft review still lists all required artifacts without fabricating readiness");
    expect(missing(ApplyScenarioForm(workspace, workspace.Map().revision, editedDocument()), CreatorPrerequisite::MapDraft), "scenario creation requires a map draft");
    expect(workspace.StartMapDraft("revision-bound bundle").has_value(), "start draft before readiness");
    expect(ApplyScenarioForm(workspace, workspace.Map().revision, readyDocument()).has_value(), "retain scenario before readiness");
    MapDocument grid{};
    RefreshMapReadiness(workspace, state, nullptr);
    expect(!state.readiness.map.ready && state.readiness.map.diagnostic == "Open or create the tile grid.", "no grid blocks the MAP input");
    RefreshMapReadiness(workspace, state, &grid);
    expect(state.readiness.map.ready && state.readiness.map.diagnostic == "32 x 32 tile grid ready.", "MAP input follows the grid pointer within one revision");
    expect(state.readiness.revision == workspace.Map().revision && workspace.Map().readiness.ready == 2, "fresh readiness without sources readies only MAP and scenario");
    for (std::size_t role = 0; role < state.readiness.roles.size(); ++role)
    {
        const auto& progress = state.readiness.roles[role];
        expect(progress.contract.ready && progress.contract.diagnostic == "Matches the layout the game loaded in a map pack trial.", "every role contract is closed by the trial");
        expect(!progress.source.ready && !workspace.Map().artifacts[static_cast<std::size_t>(MapArtifact::Heights) + role].ready, "a role without a source is not ready");
    }
    state.readiness = syntheticProofs(workspace.Map().revision);
    RefreshMapReadiness(workspace, state, &grid);
    expect(workspace.Map().readiness.ready == 6, "GPU-free refresh records current synthetic proofs in map readiness");
    const auto proofs   = state.readiness;
    const auto scenario = *workspace.Map().scenario;
    expect(workspace.ReviseMap().has_value(), "simulate a grid edit");
    const auto stale = ReviewMapReadiness(workspace.Map(), proofs);
    expect(stale.count.ready == 0 && !stale.exportEnabled && workspace.Map().readiness.ready == 0, "stale proofs cannot keep any artifact ready after a grid edit");
    RefreshMapReadiness(workspace, state, &grid);
    expect(workspace.Map().scenario == scenario && workspace.Map().readiness.ready == 2 && !state.readiness.roles[0].source.ready, "revision change preserves scenario edits and resets derived readiness proofs");
    state.readiness = syntheticProofs(workspace.Map().revision);
    RefreshMapReadiness(workspace, state, &grid);
    auto edited               = scenario;
    edited.entries.front().id = -7;
    expect(ApplyScenarioForm(workspace, workspace.Map().revision, edited).has_value() && workspace.Map().readiness.ready == 0, "scenario edits also invalidate all previously ready artifacts");
    expect(workspace.Map().scenario == edited, "the map draft owns the edited scenario");
    workspace.RequestDiscard();
    expect(workspace.ConfirmDiscard().has_value() && !workspace.Map().scenario, "confirmed map discard clears its owned scenario");
    RefreshMapReadiness(workspace, state, &grid);
    expect(!state.readiness.roles[0].source.ready, "discard cannot leak previous readiness proofs");
}

void testSingleMapScenario()
{
    ScenarioDocument document{ { ScenarioEntry{}, ScenarioEntry{} } };
    auto&            entry = document.entries.front();
    entry.title[2]         = "Isola";
    entry.description[1]   = "Une ile";
    entry.variators.front() += 5;
    SingleMapScenario(document);
    const auto& kept = document.entries.front();
    expect(document.entries.size() == 1, "a map keeps only its first scenario entry");
    expect(std::ranges::all_of(kept.title, [](const std::string& text)
                               {
                                   return text == "Isola";
                               }) &&
               std::ranges::all_of(kept.description, [](const std::string& text)
                                   {
                                       return text == "Une ile";
                                   }),
           "the first written title and description fill every language");
    auto expected = DefaultScenarioVariators();
    expected.front() += 5;
    expect(kept.variators == expected, "the first entry keeps its game rules");

    ScenarioDocument empty;
    SingleMapScenario(empty);
    expect(empty.entries.size() == 1, "a map without a scenario gets one");
}

} // namespace

int main()
{
    testSignedFormWriterRoundTrip();
    testEveryModelIssueAndDuplicateDiagnostic();
    testBindingAndOneRoleBlockExport();
    testResearchDefaultsAndRevisionInvalidation();
    testSingleMapScenario();
    if (failures != 0)
    {
        std::cerr << failures << " scenario-form test(s) failed\n";
    }
    return failures == 0 ? 0 : 1;
}
