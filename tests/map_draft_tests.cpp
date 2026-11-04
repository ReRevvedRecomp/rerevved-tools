#include "map/map_draft_store.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

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

struct TemporaryDirectory
{
    std::filesystem::path path;

    TemporaryDirectory()
    {
        static unsigned int sequence = 0;
        for (;;)
        {
            const auto token = std::chrono::steady_clock::now().time_since_epoch().count();
            path             = std::filesystem::temp_directory_path() / ("rerevved-map-draft-test-" + std::to_string(token) + "-" + std::to_string(sequence++));
            std::error_code error;
            if (std::filesystem::create_directory(path, error))
            {
                break;
            }
            if (error && error != std::errc::file_exists)
            {
                throw std::runtime_error("Could not create unique map draft test directory.");
            }
        }
    }

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
        expect(!error, "test temporary directory is removed");
    }
};

SavedMapDraft example()
{
    SavedMapDraft draft;
    draft.identity  = "Island % map\nwith a line break";
    draft.step      = MapStep::Scenario;
    draft.synthetic = false;
    for (std::size_t offset = 0; offset < kMapCoreSize; ++offset)
    {
        draft.source.core[offset] = static_cast<std::byte>(offset & 0xFF);
        draft.tiles.core[offset]  = static_cast<std::byte>((offset * 7) & 0xFF);
    }
    draft.source.footer.fill(std::byte{ 0xA5 });
    draft.tiles.footer.fill(std::byte{ 0xFF });
    ScenarioEntry entry;
    entry.map            = "T";
    entry.id             = 104;
    entry.multiplayer    = true;
    entry.title[0]       = "Caf\xC3\xA9 island";
    entry.description[4] = "Line one\r\nline two";
    entry.variators[3]   = -7;
    draft.scenario       = ScenarioDocument{ { entry, ScenarioEntry{} } };
    draft.exportForm     = { "Island map", "island-map", "1.2", "Ann", "A % test\nmap", true, true, 9, std::filesystem::path(u8"D:/Games/ReRevved/maps") };
    draft.savedAt        = 1793500000;
    return draft;
}

void testRoundTrip()
{
    const auto draft   = example();
    const auto encoded = EncodeMapDraft(draft);
    const auto decoded = DecodeMapDraft(encoded);
    expect(decoded.has_value(), "an encoded draft decodes");
    expect(decoded && *decoded == draft, "decoding restores every saved field");
    expect(encoded.find("Island %25 map%0Awith") != std::string::npos, "percent and line breaks are escaped");
    expect(encoded.find('\r') == std::string::npos, "carriage returns are escaped");

    auto legacy = encoded;
    legacy.insert(legacy.find("\nscenario") + 1, "image 0 generated " + std::string(kMapCoreSize * 2, '0') + "\nstamp 0 1 2 3 4\nimage 1 imported C:/Images/blends.png\n");
    const auto older = DecodeMapDraft(legacy);
    expect(older && *older == draft, "older image and stamp lines are ignored");
    for (const auto& [name, step] : { std::pair{ "heights", MapStep::Heights }, std::pair{ "blends", MapStep::MountainHillBlends }, std::pair{ "lightmap", MapStep::Lightmap } })
    {
        auto       imageTab = encoded;
        const auto at       = imageTab.find("step ") + 5;
        imageTab.replace(at, imageTab.find('\n', at) - at, name);
        const auto opened = DecodeMapDraft(imageTab);
        expect(opened && opened->step == step, "a draft saved on a former image tab still opens");
    }

    SavedMapDraft blank;
    blank.identity   = "Blank";
    const auto empty = DecodeMapDraft(EncodeMapDraft(blank));
    expect(empty && *empty == blank && !empty->scenario, "a draft without a scenario round-trips");

    expect(EncodeMapDraft(blank).find("\nfile") == std::string::npos, "a map file names no other file");
    SavedMapDraft recovery = blank;
    recovery.file          = std::filesystem::path(u8"D:/Maps/Caf\u00e9 % isles.rrmap");
    const auto restored    = DecodeMapDraft(EncodeMapDraft(recovery));
    expect(restored && *restored == recovery, "a recovery copy keeps the file it belongs to");

    SavedMapDraft noEntries = blank;
    noEntries.scenario      = ScenarioDocument{};
    const auto scenario     = DecodeMapDraft(EncodeMapDraft(noEntries));
    expect(scenario && scenario->scenario && scenario->scenario->entries.empty(), "an empty scenario stays distinct from none");
}

void testRejectsDamage()
{
    const auto encoded = EncodeMapDraft(example());
    expect(!DecodeMapDraft(encoded.substr(0, encoded.size() / 2)), "a truncated draft is refused");
    expect(!DecodeMapDraft("not a draft\nend\n"), "a foreign header is refused");
    auto unknown = encoded;
    unknown.insert(unknown.find("end\n"), "colour blue\n");
    expect(!DecodeMapDraft(unknown), "an unknown field is refused");
    auto badHex = encoded;
    badHex.replace(badHex.find("tiles ") + 6, 2, "zz");
    expect(!DecodeMapDraft(badHex), "bad tile bytes are refused");
    auto       noTiles = encoded;
    const auto tiles   = noTiles.find("tiles ");
    noTiles.erase(tiles, noTiles.find('\n', tiles) + 1 - tiles);
    expect(!DecodeMapDraft(noTiles), "a draft without its tiles is refused");
    auto twoFiles = encoded;
    twoFiles.insert(twoFiles.find("end\n"), "file C:/a.rrmap\nfile C:/b.rrmap\n");
    expect(!DecodeMapDraft(twoFiles), "a second file field is refused");
    auto twice = encoded;
    twice.insert(twice.find("end\n"), "identity Again\n");
    expect(!DecodeMapDraft(twice), "a repeated field is refused");
    auto       noVariators = encoded;
    const auto variators   = noVariators.find("variators ");
    noVariators.erase(variators, noVariators.find('\n', variators) + 1 - variators);
    expect(!DecodeMapDraft(noVariators), "an entry without its variators is refused");
    auto twoIds = encoded;
    twoIds.insert(twoIds.find("id 104\n"), "id 105\n");
    expect(!DecodeMapDraft(twoIds), "an entry with two IDs is refused");
    auto twoTitles = encoded;
    twoTitles.insert(twoTitles.find("title 0 "), "title 0 Other\n");
    expect(!DecodeMapDraft(twoTitles), "an entry with two titles in one locale is refused");
    auto paddedLocale = encoded;
    paddedLocale.insert(paddedLocale.find("title 0 "), "title 00 Other\n");
    expect(!DecodeMapDraft(paddedLocale), "a locale written twice in different forms is refused");
    auto       noExportSlot = encoded;
    const auto slot         = noExportSlot.find("export-slot ");
    noExportSlot.erase(slot, noExportSlot.find('\n', slot) + 1 - slot);
    expect(!DecodeMapDraft(noExportSlot), "a draft without an export field is refused");
    auto farTime = encoded;
    farTime.replace(farTime.find("saved ") + 6, std::to_string(example().savedAt).size(), "-9223372036854775808");
    expect(!DecodeMapDraft(farTime), "an out-of-range save time is refused");
#ifdef _WIN32
    // Only Windows converts paths from UTF-8 and so rejects damaged text.
    auto badPath = encoded;
    badPath.replace(badPath.find("export-save-to ") + 15, 2, "\xFF\xFE");
    expect(!DecodeMapDraft(badPath), "a path that is not UTF-8 is refused");
#endif
}

void testSaveListAndNames()
{
    TemporaryDirectory directory;
    const auto         folder = directory.path / "nested" / "Drafts";
    auto               older  = example();
    older.identity            = "Older";
    older.savedAt             = 100;
    auto newer                = example();
    newer.identity            = "Newer";
    newer.savedAt             = 200;

    const auto first = NewMapDraftPath(folder, older.savedAt);
    expect(first.extension() == kMapDraftExtension, "draft files use the draft extension");
    expect(SaveMapDraft(first, EncodeMapDraft(older)).has_value(), "saving creates the drafts folder");
    const auto second = NewMapDraftPath(folder, older.savedAt);
    expect(second != first, "a new draft name never reuses an existing file");
    expect(SaveMapDraft(second, EncodeMapDraft(newer)).has_value(), "a second draft saves");
    expect(SaveMapDraft(first, EncodeMapDraft(older)).has_value(), "saving replaces the draft's own file");
    auto huge                          = older;
    huge.scenario->entries[0].title[1] = std::string(1U << 20, 'x');
    expect(!SaveMapDraft(first, EncodeMapDraft(huge)), "a draft the loader would refuse is not saved");
    expect(LoadMapDraft(first).has_value(), "a refused save keeps the previous file");
    {
        std::ofstream damaged(folder / ("damaged" + std::string(kMapDraftExtension)), std::ios::binary);
        damaged << "ReRevved Tools map draft 1\nidentity Half\n";
    }
    {
        std::ofstream other(folder / "notes.txt", std::ios::binary);
        other << "not a draft";
    }
    const auto drafts = ListMapDrafts(folder);
    expect(drafts.size() == 3, "only draft files are listed");
    expect(drafts.size() == 3 && drafts[0].draft.identity == "Newer" && drafts[1].draft.identity == "Older", "drafts list newest first");
    expect(drafts.size() == 3 && !drafts[2].error.empty(), "an unreadable draft lists last with its error");
    std::error_code error;
    expect(!std::filesystem::exists(first.string() + ".tmp", error), "no temporary file is left behind");
    {
        std::ofstream other(first.string() + ".tmp", std::ios::binary);
        other << "someone else's file";
    }
    expect(SaveMapDraft(first, EncodeMapDraft(older)).has_value() && std::filesystem::file_size(first.string() + ".tmp", error) == 19, "a file with the temporary name is left alone");
    expect(ListMapDrafts(directory.path / "missing").empty(), "a missing folder lists nothing");
}

void testRecentMaps()
{
    TemporaryDirectory directory;
    const auto         list = RecentMapsFile(directory.path);
    expect(LoadRecentMaps(list).empty(), "a missing recent list is empty");
    std::vector<std::filesystem::path> recent;
    for (int index = 0; index < 12; ++index)
    {
        RememberRecentMap(recent, directory.path / ("map-" + std::to_string(index) + ".rrmap"));
    }
    expect(recent.size() == kRecentMapLimit && recent.front().filename() == "map-11.rrmap", "the newest map leads and the list is capped");
    RememberRecentMap(recent, directory.path / "sub" / ".." / "map-5.rrmap");
    const auto fives = std::ranges::count_if(recent, [](const std::filesystem::path& path)
                                             {
                                                 return path.filename() == "map-5.rrmap";
                                             });
    expect(recent.size() == kRecentMapLimit && recent.front().filename() == "map-5.rrmap" && fives == 1, "a map remembered again moves to the front once");
    recent.back() = directory.path / std::filesystem::path(u8"Caf\u00e9 % map.rrmap");
    expect(SaveRecentMaps(list, recent).has_value(), "the recent list saves");
    expect(LoadRecentMaps(list) == recent, "the recent list round-trips");
    {
        std::ofstream foreign(list, std::ios::binary);
        foreign << "C:/not/a/list.rrmap\n";
    }
    expect(LoadRecentMaps(list).empty(), "a file without the list header is ignored");

    const std::array files{ directory.path / "good.rrmap", directory.path / "missing.rrmap", directory.path / "damaged.rrmap" };
    expect(SaveMapDraft(files[0], EncodeMapDraft(example())).has_value(), "a listed map saves");
    {
        std::ofstream damaged(files[2], std::ios::binary);
        damaged << "ReRevved Tools map draft 1\nidentity Half\n";
    }
    const auto listed = ListMapFiles(files);
    expect(listed.size() == 2 && listed[0].path == files[0] && listed[0].error.empty() && listed[0].draft == example(), "a listed map is read");
    expect(listed.size() == 2 && listed[1].path == files[2] && !listed[1].error.empty(), "a missing map is left out and an unreadable one carries its error");
}

void testFileNames()
{
    expect(MapFileName("Smoke Isles") == std::filesystem::path("Smoke Isles.rrmap"), "a plain name becomes the file name");
    expect(MapFileName("a/b:c?. ") == std::filesystem::path("a_b_c_.rrmap"), "characters Windows refuses are replaced and trailing dots dropped");
    expect(MapFileName("..") == std::filesystem::path("Untitled.rrmap"), "a name with nothing left is Untitled");
    expect(MapFileName("con") == std::filesystem::path("con_.rrmap") && MapFileName("LPT1") == std::filesystem::path("LPT1_.rrmap"), "Windows device names are not used");
    expect(MapFileName("Console") == std::filesystem::path("Console.rrmap"), "a name that only starts like a device name is kept");
    expect(MapRecoveryFolder({}).empty() && RecentMapsFile({}).empty(), "no data folder means no recovery or recent list");
}

void testAge()
{
    expect(FormatMapDraftAge(1000, 1010) == "just now", "seconds read as just now");
    expect(FormatMapDraftAge(1000, 1000 + 60) == "1 minute ago", "one minute is singular");
    expect(FormatMapDraftAge(1000, 1000 + 7200) == "2 hours ago", "hours are counted");
    expect(FormatMapDraftAge(1000, 1000 + 90000) == "yesterday", "one day reads as yesterday");
    expect(FormatMapDraftAge(1000, 1000 + 5 * 86400) == "5 days ago", "days are counted");
    expect(FormatMapDraftAge(2000, 1000) == "just now", "a clock behind the save reads as just now");
}

} // namespace

int main()
{
    testRoundTrip();
    testRejectsDamage();
    testSaveListAndNames();
    testRecentMaps();
    testFileNames();
    testAge();
    if (failures != 0)
    {
        std::cerr << failures << " map draft assertion(s) failed.\n";
        return 1;
    }
    std::cout << "Map draft tests passed.\n";
    return 0;
}
