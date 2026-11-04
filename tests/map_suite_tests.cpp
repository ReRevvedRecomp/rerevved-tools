#include "map/map_suite.h"

#include "asset_pack_writer.h"
#include "map/map_edit_model.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
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

constexpr std::string_view kPlan = R"(# Two packs
[pack]
id = suite-one
name = Suite One
author = Tester
slot = 6

[map]
land = Pangaea
id = 101
map = rr_pangaea
title = Pangaea
description = One landmass.
displaycard = 4352
multiplayer = yes

[map]
land = Skirmish
players = 4
teams = yes
mirror = Left to Right
id = 102
map = rr_skirmish
title = Skirmish

[pack]
id = suite-two
name = Suite Two
slot = 7

[map]
land = Earth
players = 7
id = 103
map = rr_earth
title = Earth
)";

std::string parseError(std::string_view text)
{
    const auto plan = ParseMapSuitePlan(text);
    return plan ? std::string() : plan.error();
}

void testParse()
{
    const auto plan = ParseMapSuitePlan(kPlan);
    expect(plan.has_value(), "the plan parses: " + (plan ? std::string() : plan.error()));
    if (!plan)
    {
        return;
    }
    expect(plan->size() == 2 && (*plan)[0].maps.size() == 2 && (*plan)[1].maps.size() == 1, "maps belong to the pack above them");
    const auto& pack  = (*plan)[0];
    const auto& first = pack.maps[0];
    expect(pack.settings.id == "suite-one" && pack.settings.name == "Suite One" && pack.settings.author == "Tester" && pack.settings.slot == 6 &&
               pack.settings.version == "1.0",
           "pack keys and the default version");
    expect(first.land == StartingLand::Pangaea && first.size == LandSize::Medium && first.mirror == MapMirror::Off && first.players == 5 &&
               first.seed == kFirstLayoutSeed && first.displayCard == 4352 && first.multiplayer == true && first.description == "One landmass." && first.line == 8,
           "map keys and the Create tab defaults");
    expect(pack.maps[1].mirror == MapMirror::LeftToRight && pack.maps[1].players == 4 && pack.maps[1].teams && !pack.maps[1].displayCard, "a team map mirrors");

    expect(ParseMapSuitePlan("\xEF\xBB\xBF[pack]\r\nid = a\r\nname = A\r\nslot = 6\r\n[map]\r\nland = Pangaea\r\nid = 101\r\nmap = rr_a\r\ntitle = A\r\n").has_value(),
           "a byte order mark and CRLF lines parse");
    expect(parseError("[pack]\nid = a\nname = A\nslot = 6\n").starts_with("Line 1: the pack has no [map]"), "a pack needs a map");
    expect(parseError("[pack]\nid = a\nname = A\nslot = 6\n[map]\nland = Pangaea\nid = 101\nmap = rr_a\n") == "Line 5: the section has no title.", "a map needs a title");
    expect(parseError("[pack]\nid = a\nname = A\ncolour = red\n") == "Line 4: a [pack] has no key colour.", "unknown keys are refused");
    expect(parseError("[pack]\nid = a\nid = b\n") == "Line 3: id is set twice.", "repeated keys are refused");
    expect(parseError("[pack]\nid = a\nname =\n") == "Line 3: name has no value.", "empty values are refused");
    expect(parseError("[map]\n") == "Line 1: a [map] comes before any [pack].", "maps need a pack");
    expect(parseError("[pack]\nid = a\nname = A\nslot = 6\n[map]\nland = Atlantis\n") == "Line 6: no starting land is named Atlantis.", "lands are named as on the Create tab");
    expect(parseError("[pack]\nid = a\nname = A\nslot = 6\n[map]\nmirror = Top to Bottom\nland = Terra\n") == "Line 7: Terra takes mirror = Off.", "a land that never mirrors refuses a mirror");
    expect(parseError("[pack]\nid = a\nname = A\nslot = 6\n[map]\nplayers = 8\n") == "Line 6: players is 2 to 7.", "players stay 2 to 7");
    expect(parseError("[pack]\nid = a\nname = A\nslot = 6\n[map]\nland = Pangaea\nid = 101\nmap = rr_a\ntitle = A\nteams = yes\nplayers = 4\n") ==
               "Line 5: teams needs a mirror and an even player count.",
           "teams need a mirror");
    expect(parseError("[pack]\nid = a\nname = A\nslot = 6\n[map]\nsource = a.rrmap\nland = Pangaea\n") == "Line 5: a map with a source takes no land.", "an imported map takes no land");
    expect(ParseMapSuitePlan("[pack]\nid = a\nname = A\nslot = 6\n[map]\nsource = a.rrmap\n").has_value(), "an imported map needs only its source");
    expect(parseError("[pack]\nid = a\nname = A\nslot = 6\n[map]\nland = Pangaea\nid = 100\nmap = rr_a\ntitle = A\nrandomize = 3\n") == "Line 5: randomize needs a source.",
           "only an imported map rerolls its terrain");
}

void testClashes()
{
    auto plan = ParseMapSuitePlan(kPlan);
    if (!plan)
    {
        return;
    }
    expect(MapSuiteClashes(*plan, {}).empty(), "a clean plan has no clashes");
    auto clashing             = *plan;
    clashing[1].settings.slot = 6;
    clashing[1].settings.id   = "Suite-One";
    clashing[1].maps[0].id    = 101;
    clashing[1].maps[0].map   = "RR_PANGAEA";
    const auto clashes        = MapSuiteClashes(clashing, {});
    expect(clashes.size() == 4, "pack ID, slot, scenario ID and MAP repeats are each listed");

    MapPackUsage usage;
    usage.packIds     = { "suite-two" };
    usage.scenarioIds = { 103 };
    usage.mapNames    = { "RR_Earth" };
    usage.slots       = { 6 };
    expect(MapSuiteClashes(*plan, usage).size() == 4, "names and slots already in use clash");
}

void testExport()
{
    static std::atomic<int> counter{ 0 };
    const auto              root = std::filesystem::temp_directory_path() / ("rerevved-map-suite-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(counter++));
    std::filesystem::create_directories(root);
    const auto plan = ParseMapSuitePlan(kPlan);
    if (!plan)
    {
        return;
    }

    auto broken            = *plan;
    broken[1].maps[0].land = StartingLand::Blank;
    const auto refused     = ExportMapSuite(broken, root, {});
    expect(!refused && std::filesystem::is_empty(root), "a map that cannot export writes no pack at all");

    const auto receipts = ExportMapSuite(*plan, root, {});
    expect(receipts && receipts->size() == 2, "both packs are written: " + (receipts ? std::string() : receipts.error()));
    if (receipts)
    {
        expect(ValidateAssetPack(root / "suite-one").empty() && ValidateAssetPack(root / "suite-two").empty(), "written packs validate");
        expect((*receipts)[0].mapName == "rr_pangaea, rr_skirmish", "a pack holds every map of its section");
        const auto again = ExportMapSuite(*plan, root, {});
        expect(!again && again.error().starts_with("The plan clashes"), "existing packs in the output folder are never replaced");

        const auto other = root / "other";
        std::filesystem::create_directories(other);
        const auto saved = root / "saved";
        std::filesystem::create_directories(saved);
        const auto repeat = ExportMapSuite(*plan, other, {}, saved);
        expect(repeat && (*repeat)[0].sha256 == (*receipts)[0].sha256 && (*repeat)[1].sha256 == (*receipts)[1].sha256, "the same plan writes the same bytes");
        expect(std::filesystem::is_regular_file(saved / "suite-one--rr_pangaea.rrmap") && std::filesystem::is_regular_file(saved / "suite-two--rr_earth.rrmap"),
               "every map is saved for the editor");
        const auto savedAgain = root / "saved-again";
        std::filesystem::create_directories(savedAgain);
        const auto twice = ExportMapSuite(*plan, savedAgain, {}, saved);
        expect(!twice && std::filesystem::is_empty(savedAgain), "saved map files are never replaced");

        auto imported = ParseMapSuitePlan("[pack]\nid = imported\nname = Imported\nslot = 8\n[map]\nsource = suite-two--rr_earth.rrmap\ntitle = Earth Again\n");
        expect(imported && ResolveMapSuiteSources(*imported, saved).has_value(), "a saved map file is read as a source");
        if (imported)
        {
            const auto& map = (*imported)[0].maps[0];
            expect(map.map == "rr_earth" && map.id == 103 && map.sourceTiles && MapStartMarkerCount(*map.sourceTiles) == 7, "the source's MAP, ID and grid carry over");
            const auto into = root / "imported";
            std::filesystem::create_directories(into);
            const auto written = ExportMapSuite(*imported, into, {});
            expect(written && ValidateAssetPack(into / "imported").empty(), "an imported map exports: " + (written ? std::string() : written.error()));
            const auto pack = LoadMapPack(into / "imported");
            expect(pack && pack->scenario && pack->scenario->entries.front().title.front() == "Earth Again" && pack->tiles.core == map.sourceTiles->core,
                   "the plan's title replaces the source's and the grid is unchanged");
        }

        auto rerolled = ParseMapSuitePlan("[pack]\nid = rerolled\nname = Rerolled\nslot = 9\n[map]\nsource = suite-two--rr_earth.rrmap\nrandomize = 5\n");
        expect(rerolled && ResolveMapSuiteSources(*rerolled, saved).has_value(), "a rerolled source is read");
        if (rerolled)
        {
            const auto into = root / "rerolled";
            std::filesystem::create_directories(into);
            const auto written  = ExportMapSuite(*rerolled, into, {});
            auto       expected = *(*rerolled)[0].maps[0].sourceTiles;
            RandomizeLandTerrain(expected, 5);
            const auto pack = LoadMapPack(into / "rerolled");
            expect(written && pack && pack->tiles.core == expected.core && expected.core != (*rerolled)[0].maps[0].sourceTiles->core,
                   "randomize rerolls the imported land terrain with its seed");
        }
    }

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

} // namespace

int main()
{
    testParse();
    testClashes();
    testExport();
    if (failures != 0)
    {
        std::cerr << failures << " map suite test(s) failed\n";
    }
    return failures == 0 ? 0 : 1;
}
