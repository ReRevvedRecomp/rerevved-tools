#include "map/map_pack_export.h"

#include "asset_pack_writer.h"
#include "heights_dds.h"
#include "map/scenario_form.h"
#include "map_bundle_writer.h"
#include "terrain_dds.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
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

bool contains(const std::vector<std::string>& lines, std::string_view text)
{
    return std::ranges::find(lines, text) != lines.end();
}

std::vector<std::byte> solidDds(TerrainTexture role)
{
    Rgba8Image image;
    image.width  = TerrainTextureSide(role);
    image.height = image.width;
    image.rgba8.assign(std::size_t(image.width) * image.height * 4, std::byte{ 0x40 });
    auto dds = EncodeTerrainDds(role, image);
    if (!dds)
    {
        std::cerr << "synthetic DDS failed\n";
        std::exit(2);
    }
    return std::move(*dds);
}

MapDocument readyMap();

MapDraft readyDraft()
{
    MapDraft draft;
    draft.started  = true;
    draft.revision = 7;
    draft.identity = "synthetic map";
    ScenarioEntry entry;
    entry.map            = "Q";
    entry.id             = 100;
    entry.title[0]       = "Synthetic";
    entry.description[0] = "Synthetic map";
    draft.scenario       = ScenarioDocument{ { entry } };

    MapHeightsSource heights;
    heights.samples.assign(kHeightsDdsSampleCount, 1234);
    heights.readBackPassed = true;
    heights.generatedFrom  = readyMap().core;
    draft.heights          = heights;

    for (const auto role : { TerrainTexture::MountainHillBlends, TerrainTexture::Lightmap })
    {
        MapTerrainSource source;
        source.dds                                    = solidDds(role);
        source.readBackPassed                         = true;
        source.generatedFrom                          = readyMap().core;
        draft.terrain[static_cast<std::size_t>(role)] = std::move(source);
    }
    return draft;
}

MapDocument readyMap()
{
    MapDocument map{};
    for (std::size_t index = 0; index < map.core.size(); ++index)
    {
        map.core[index] = static_cast<std::byte>(index % 7);
    }
    map.footer.fill(std::byte{ 0x11 });
    // Two start markers, the fewest a pack may export with.
    map.core[100] |= std::byte{ 0x20 };
    map.core[700] |= std::byte{ 0x20 };
    return map;
}

MapPackSettings readySettings()
{
    MapPackSettings settings;
    settings.id   = "synthetic-pack";
    settings.name = "Synthetic Pack";
    settings.slot = 6;
    return settings;
}

void testBlockers()
{
    const auto draft    = readyDraft();
    const auto map      = readyMap();
    const auto settings = readySettings();
    expect(MapPackBlockers(draft, &map, settings).empty(), "ready draft has no blockers");

    expect(MapPackBlockers(MapDraft{}, &map, settings) == std::vector<std::string>{ "Start a map first." }, "unstarted draft returns only the start blocker");
    expect(contains(MapPackBlockers(draft, nullptr, settings), "Open or create the tile grid."), "missing grid");

    auto noScenario     = readyDraft();
    noScenario.scenario = std::nullopt;
    expect(contains(MapPackBlockers(noScenario, &map, settings), "The map has no scenario entry."), "missing scenario");
    noScenario.scenario = ScenarioDocument{};
    expect(contains(MapPackBlockers(noScenario, &map, settings), "The map has no scenario entry."), "empty scenario");

    auto invalid                          = readyDraft();
    invalid.scenario->entries[0].title[0] = "Bad & literal";
    const auto issues                     = ValidateScenarioDocument(*invalid.scenario);
    const auto listed                     = MapPackBlockers(invalid, &map, settings);
    expect(!issues.empty() && contains(listed, "Title: " + std::string(ScenarioErrorMessage(issues.front().code))), "scenario validation issue is listed by its row");
    auto everyLanguage = invalid;
    everyLanguage.scenario->entries[0].title.fill("Bad & literal");
    const auto once = MapPackBlockers(everyLanguage, &map, settings);
    expect(std::ranges::count_if(once, [](const std::string& blocker)
                                 {
                                     return blocker.starts_with("Title: ");
                                 }) == 1,
           "a title problem in every language is listed once");
    auto oneMarker = map;
    oneMarker.core[700] &= std::byte{ 0xDF };
    expect(contains(MapPackBlockers(draft, &oneMarker, settings), "Place at least 2 start markers on the Grid."), "one start marker blocks export");

    auto mixed = readyDraft();
    mixed.scenario->entries.push_back(mixed.scenario->entries[0]);
    mixed.scenario->entries[1].id  = 101;
    mixed.scenario->entries[1].map = "R";
    expect(contains(MapPackBlockers(mixed, &map, settings), "Map Letter: choose one letter, A to Z."), "mixed MAP names");
    auto none = readyDraft();
    for (auto& entry : none.scenario->entries)
    {
        entry.map = "NONE";
    }
    expect(contains(MapPackBlockers(none, &map, settings), "Map Letter: choose one letter, A to Z."), "NONE MAP name");

    auto low                    = readyDraft();
    low.scenario->entries[0].id = 99;
    expect(contains(MapPackBlockers(low, &map, settings), "Scenario ID: 100 or higher. Lower IDs belong to the game."), "low scenario ID");

    const std::string generating = "Generating map images...";
    const std::string unreadable = "A map image failed its read back check.";
    const auto        count      = [](const std::vector<std::string>& blockers, const std::string& line)
    {
        return std::ranges::count(blockers, line);
    };
    const auto current = readyDraft();
    expect(MapPackBlockers(current, &map, settings).empty(), "current images give no blocker");

    auto unmarked                      = readyDraft();
    unmarked.heights->generatedFrom    = std::nullopt;
    unmarked.terrain[0]->generatedFrom = std::nullopt;
    unmarked.terrain[1]->generatedFrom = std::nullopt;
    expect(MapPackBlockers(unmarked, &map, settings) == std::vector<std::string>{ generating }, "images with no origin give the generating line once");

    auto noImages = readyDraft();
    noImages.heights.reset();
    noImages.terrain[0].reset();
    noImages.terrain[1].reset();
    const auto missing = MapPackBlockers(noImages, &map, settings);
    expect(count(missing, generating) == 1 && missing.size() == 1, "no images give the generating line once and no other");
    auto noBlends       = readyDraft();
    noBlends.terrain[0] = std::nullopt;
    expect(count(MapPackBlockers(noBlends, &map, settings), generating) == 1, "one missing image gives the generating line");

    auto otherTiles                      = readyDraft();
    auto origin                          = map.core;
    origin[5]                            = std::byte{ 0x33 };
    otherTiles.heights->generatedFrom    = origin;
    otherTiles.terrain[0]->generatedFrom = origin;
    otherTiles.terrain[1]->generatedFrom = origin;
    expect(MapPackBlockers(otherTiles, &map, settings) == std::vector<std::string>{ generating }, "images from other tiles give the generating line once");

    auto failedHeights                    = readyDraft();
    failedHeights.heights->readBackPassed = false;
    expect(MapPackBlockers(failedHeights, &map, settings) == std::vector<std::string>{ unreadable }, "heights without read back");
    auto failedBoth                       = readyDraft();
    failedBoth.terrain[0]->readBackPassed = false;
    failedBoth.terrain[1]->readBackPassed = false;
    expect(MapPackBlockers(failedBoth, &map, settings) == std::vector<std::string>{ unreadable }, "failed read backs give one line");

    auto bad = settings;
    bad.id   = "";
    expect(contains(MapPackBlockers(draft, &map, bad), "Pack ID: enter one."), "empty id");
    for (const char* id : { "Upper", "a--b", "-a", "a-", "a b" })
    {
        bad.id = id;
        expect(contains(MapPackBlockers(draft, &map, bad), "Pack ID: use lowercase letters, digits and single dashes."), "invalid id shape");
    }
    bad.id = std::string(40, 'a');
    expect(contains(MapPackBlockers(draft, &map, bad), "Pack ID: 39 characters at most."), "id over 39");
    bad.id = std::string(39, 'a');
    expect(MapPackBlockers(draft, &map, bad).empty(), "39-character id passes");

    bad      = settings;
    bad.name = "  ";
    expect(contains(MapPackBlockers(draft, &map, bad), "Title: enter one."), "blank name");
    for (const char* version : { "1", "1.", ".1", "1.0.0", "a.b" })
    {
        bad         = settings;
        bad.version = version;
        expect(contains(MapPackBlockers(draft, &map, bad), "Version: major.minor, for example 1.0."), "invalid version");
    }
    for (const std::uint32_t slot : { 4U, 32U })
    {
        bad      = settings;
        bad.slot = slot;
        expect(contains(MapPackBlockers(draft, &map, bad), "Scenario File Slot: 5 to 31."), "slot outside 5..31");
    }
    for (const std::uint32_t slot : { 5U, 31U })
    {
        bad      = settings;
        bad.slot = slot;
        expect(MapPackBlockers(draft, &map, bad).empty(), "slot 5 and 31 accepted");
    }
}

void testAssembly()
{
    const auto draft    = readyDraft();
    const auto map      = readyMap();
    const auto settings = readySettings();
    const auto assembly = AssembleMapPack(draft, map, settings);
    expect(assembly.has_value(), "ready draft assembles");
    if (!assembly)
    {
        return;
    }
    const std::vector<std::string> names{ "DLCScenarioData6.xml", "Q.map", "MapQ_Heights.dds", "MapQ_MountainHill_Blends.dds", "MapQ_Lightmap.dds" };
    expect(assembly->entryNames == names && assembly->mapName == "Q", "entry names are in archive order");
    expect(assembly->sha256 == CalculateMapBundleSha256(assembly->archive), "sha256 matches the archive");

    auto bad           = settings;
    bad.id             = "";
    const auto refused = AssembleMapPack(draft, map, bad);
    expect(!refused && refused.error() == "Pack ID: enter one.", "blocked settings return the first blocker");
}

std::vector<char> readAll(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return std::vector<char>((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

void testExport()
{
    static std::atomic<int> counter{ 0 };
    const auto              root = std::filesystem::temp_directory_path() / ("rerevved-map-pack-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(counter++));
    std::filesystem::create_directories(root);

    const auto draft    = readyDraft();
    const auto map      = readyMap();
    const auto settings = readySettings();
    const auto assembly = AssembleMapPack(draft, map, settings);
    const auto receipt  = ExportMapPack(draft, map, settings, root);
    expect(receipt.published && assembly, "export publishes the pack: " + receipt.diagnostic);
    if (receipt.published && assembly)
    {
        const auto pack = root / settings.id;
        expect(receipt.packDirectory == pack && receipt.revision == 7 && receipt.identity == "synthetic map" && receipt.slot == 6 && receipt.mapName == "Q", "receipt fields");
        expect(receipt.archiveBytes == assembly->archive.size() && receipt.sha256 == assembly->sha256, "receipt size and hash");
        expect(std::filesystem::is_regular_file(pack / "asset-pack.toml"), "manifest exists");
        const auto bytes = readAll(pack / "assets" / "map-pack" / "dlc.fpk");
        expect(bytes.size() == assembly->archive.size() && std::equal(bytes.begin(), bytes.end(), reinterpret_cast<const char*>(assembly->archive.data())), "dlc.fpk equals the assembly");
        expect(ValidateAssetPack(pack).empty(), "written pack validates without warnings");
        const auto manifest = readAll(pack / "asset-pack.toml");
        expect(std::string(manifest.begin(), manifest.end()).ends_with("\n[map_pack]\nslot = 6\n\n[[map_pack.scenarios]]\nid = 100\nname = \"Synthetic\"\n"),
               "manifest carries the [map_pack] display table");
        expect(receipt.diagnostic.starts_with("Map pack written and read back.\nCopy the synthetic-pack folder"), "success diagnostic carries install steps");
        const auto text = FormatMapPackReceipt(receipt);
        expect(text.find(receipt.sha256) != std::string::npos && text.find("revision 7") != std::string::npos, "formatted receipt");

        for (const auto& path : { pack, pack / "asset-pack.toml", pack / "assets" / "map-pack" / "dlc.fpk" })
        {
            const auto opened = LoadMapPack(path);
            expect(opened.has_value(), "exported pack opens: " + (opened ? std::string() : opened.error()));
            if (opened)
            {
                // Export writes an all 0xFF footer.
                const auto footer = [](const MapDocument& document)
                {
                    return std::ranges::all_of(document.footer, [](std::byte value)
                                               {
                                                   return value == std::byte{ 0xFF };
                                               });
                };
                expect(opened->tiles.core == map.core && opened->source.core == map.core && footer(opened->tiles) && opened->scenario == draft.scenario,
                       "opened pack holds the exported tiles and scenario");
                expect(opened->identity == "Synthetic" && opened->exportForm.id == "synthetic-pack" && opened->exportForm.name == "Synthetic Pack" &&
                           opened->exportForm.slot == 6 && opened->exportForm.outputParent == root.lexically_normal(),
                       "opened pack fills the export form");
            }
        }
        const auto loose = root / "loose";
        std::filesystem::create_directories(loose);
        std::filesystem::copy_file(pack / "assets" / "map-pack" / "dlc.fpk", loose / "dlc.fpk");
        expect(!LoadMapPack(loose / "dlc.fpk"), "a dlc.fpk outside assets/map-pack is refused");
        expect(LoadMapPack(pack / "").has_value(), "a pack folder with a trailing separator opens");
        const auto notPack = LoadMapPack(root);
        expect(!notPack && notPack.error().starts_with("It is not a map pack Tools can read."), "a folder that is not a pack is refused");

        const auto again = ExportMapPack(draft, map, settings, root);
        expect(!again.published && again.diagnostic.starts_with("Pack not written: "), "second export is refused");
        expect(readAll(pack / "assets" / "map-pack" / "dlc.fpk") == bytes, "existing pack is unchanged");
    }

    auto third                                   = settings;
    third.id                                     = "third-pack";
    const std::filesystem::path protectedRoot    = root / "third-pack";
    const auto                  protectedReceipt = ExportMapPack(readyDraft(), map, third, root, std::span<const std::filesystem::path>(&protectedRoot, 1));
    expect(!protectedReceipt.published && protectedReceipt.diagnostic == "The pack folder overlaps a game, source or reference folder. Choose another folder." && !std::filesystem::exists(root / "third-pack"),
           "protected root overlap is refused and nothing is written");

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void testMultiMapPack()
{
    static std::atomic<int> counter{ 0 };
    const auto              root = std::filesystem::temp_directory_path() / ("rerevved-map-pack-multi-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(counter++));
    std::filesystem::create_directories(root);

    auto first                           = readyDraft();
    first.scenario->entries[0].map       = "rr_first";
    first.scenario->entries[0].id        = 101;
    auto second                          = readyDraft();
    second.identity                      = "second map";
    second.scenario->entries[0].map      = "rr_second";
    second.scenario->entries[0].id       = 102;
    second.scenario->entries[0].title[0] = "Second";
    const auto                      map  = readyMap();
    const std::array<MapPackMap, 2> maps{ MapPackMap{ &first, &map }, MapPackMap{ &second, &map } };
    const auto                      settings = readySettings();

    const auto                     assembly = AssembleMapPack(maps, settings);
    const std::vector<std::string> names{ "DLCScenarioData6.xml",
                                          "rr_first.map",
                                          "Maprr_first_Heights.dds",
                                          "Maprr_first_MountainHill_Blends.dds",
                                          "Maprr_first_Lightmap.dds",
                                          "rr_second.map",
                                          "Maprr_second_Heights.dds",
                                          "Maprr_second_MountainHill_Blends.dds",
                                          "Maprr_second_Lightmap.dds" };
    expect(assembly && assembly->entryNames == names && assembly->mapName == "rr_first, rr_second", "two maps share one archive and one scenario file");

    const auto receipt = ExportMapPack(maps, settings, root);
    expect(receipt.published, "multi-map export publishes: " + receipt.diagnostic);
    if (receipt.published)
    {
        const auto pack = root / settings.id;
        expect(ValidateAssetPack(pack).empty(), "multi-map pack validates");
        const auto manifest = ReadAssetPackManifest(pack);
        expect(manifest && manifest->mapPack && manifest->mapPack->scenarios.size() == 2 && manifest->mapPack->scenarios[1].id == 102 &&
                   manifest->mapPack->scenarios[1].name == "Second",
               "manifest lists every map's scenario");
    }

    auto clash                     = second;
    clash.scenario->entries[0].map = "RR_FIRST";
    const std::array<MapPackMap, 2> clashing{ MapPackMap{ &first, &map }, MapPackMap{ &clash, &map } };
    expect(!AssembleMapPack(clashing, settings), "maps in one pack need distinct MAP names");
    clash                         = second;
    clash.scenario->entries[0].id = 101;
    const std::array<MapPackMap, 2> sameId{ MapPackMap{ &first, &map }, MapPackMap{ &clash, &map } };
    expect(!AssembleMapPack(sameId, settings), "maps in one pack need distinct scenario IDs");

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

} // namespace

int main()
{
    testBlockers();
    testAssembly();
    testExport();
    testMultiMapPack();
    if (failures != 0)
    {
        std::cerr << failures << " map-pack test(s) failed\n";
    }
    return failures == 0 ? 0 : 1;
}
