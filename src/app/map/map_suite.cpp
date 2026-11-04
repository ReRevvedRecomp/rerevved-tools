#include "map/map_suite.h"

#include "map/creator_workspace.h"
#include "map/map_draft_store.h"
#include "map/map_edit_model.h"
#include "map/map_images.h"

#include <algorithm>
#include <charconv>
#include <limits>
#include <set>
#include <utility>

namespace rerevved::tools
{
namespace
{

// In LandSize and MapMirror order, as the Create tab names them.
constexpr std::array<std::string_view, 3> kSizeNames{ "Small", "Medium", "Large" };
constexpr std::array<std::string_view, 3> kMirrorNames{ "Off", "Left to Right", "Top to Bottom" };

std::string_view trim(std::string_view text)
{
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string_view::npos)
    {
        return {};
    }
    return text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
}

std::string lower(std::string_view text)
{
    std::string result(text);
    std::ranges::transform(result, result.begin(), [](char character)
                           {
                               return character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a') : character;
                           });
    return result;
}

template <typename Number>
bool parseNumber(std::string_view text, Number& value)
{
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc() && end == text.data() + text.size();
}

template <std::size_t Count>
bool parseName(std::string_view text, const std::array<std::string_view, Count>& names, int& value)
{
    const auto found = std::ranges::find(names, text);
    value            = static_cast<int>(found - names.begin());
    return found != names.end();
}

struct BuiltMap
{
    MapDraft    draft;
    MapDocument tiles;
};

std::string firstText(const ScenarioLiterals& texts)
{
    const auto found = std::ranges::find_if(texts, [](const std::string& text)
                                            {
                                                return !text.empty();
                                            });
    return found != texts.end() ? *found : std::string();
}

std::expected<BuiltMap, std::string> build(const MapSuiteMap& plan)
{
    if (!plan.source.empty() && (!plan.sourceTiles || !plan.sourceEntry))
    {
        return std::unexpected("Line " + std::to_string(plan.line) + ": the source map was not read.");
    }
    // An imported map keeps its own texts in every language unless the plan replaces them.
    ScenarioEntry entry = plan.sourceEntry.value_or(ScenarioEntry{});
    entry.map           = plan.map;
    entry.id            = plan.id;
    if (!plan.title.empty())
    {
        entry.title.fill(plan.title);
    }
    if (!plan.description.empty() || !plan.sourceEntry)
    {
        entry.description.fill(plan.description);
    }
    if (plan.displayCard)
    {
        entry.variators[30] = *plan.displayCard;
    }
    entry.multiplayer = plan.multiplayer.value_or(entry.multiplayer);
    CreatorWorkspace workspace;
    BuiltMap         built;
    built.tiles = plan.sourceTiles ? *plan.sourceTiles : GenerateStartingLand(plan.land, plan.size, plan.seed, plan.mirror, plan.players, plan.teams);
    if (plan.sourceTiles && plan.randomize)
    {
        RandomizeLandTerrain(built.tiles, *plan.randomize);
    }
    if (!workspace.StartMapDraft(firstText(entry.title)) || !workspace.SetMapScenario(workspace.Map().revision, ScenarioDocument{ { std::move(entry) } }))
    {
        return std::unexpected("Line " + std::to_string(plan.line) + ": the map could not be started.");
    }
    // Some land and team choices leave no room for every start.
    if (const auto starts = MapStartMarkerCount(built.tiles); !plan.sourceTiles && starts != static_cast<std::size_t>(plan.players))
    {
        return std::unexpected("Line " + std::to_string(plan.line) + ": only " + std::to_string(starts) + " of " + std::to_string(plan.players) + " starts fit. Try another seed.");
    }
    if (const auto generated = GenerateMapTerrain(workspace, built.tiles); !generated)
    {
        return std::unexpected("Line " + std::to_string(plan.line) + ": " + generated.error());
    }
    built.draft = workspace.Map();
    return built;
}

std::expected<std::vector<BuiltMap>, std::string> buildPack(const MapSuitePack& pack)
{
    std::vector<BuiltMap> built;
    for (const auto& map : pack.maps)
    {
        auto one = build(map);
        if (!one)
        {
            return std::unexpected(one.error());
        }
        built.push_back(std::move(*one));
    }
    return built;
}

std::filesystem::path mapFile(const std::filesystem::path& folder, const MapSuitePack& pack, const MapSuiteMap& map)
{
    return folder / (pack.settings.id + "--" + map.map + std::string(kMapDraftExtension));
}

std::vector<MapPackMap> packMaps(const std::vector<BuiltMap>& built)
{
    std::vector<MapPackMap> maps;
    for (const auto& one : built)
    {
        maps.push_back({ &one.draft, &one.tiles });
    }
    return maps;
}

} // namespace

std::expected<void, std::string> ResolveMapSuiteSources(std::span<MapSuitePack> plan, const std::filesystem::path& base)
{
    for (auto& pack : plan)
    {
        for (auto& map : pack.maps)
        {
            if (map.source.empty())
            {
                continue;
            }
            const auto path  = map.source.is_absolute() ? map.source : base / map.source;
            const bool draft = lower(path.extension().string()) == kMapDraftExtension;
            auto       saved = draft ? LoadMapDraft(path) : LoadMapPack(path);
            if (!saved || !saved->scenario || saved->scenario->entries.size() != 1)
            {
                return std::unexpected("Line " + std::to_string(map.line) + ": " + path.string() + " is not a map with one scenario. " + (saved ? std::string() : saved.error()));
            }
            const auto& entry = saved->scenario->entries.front();
            map.map           = map.map.empty() ? entry.map : map.map;
            map.id            = map.id == 0 ? entry.id : map.id;
            map.sourceTiles   = saved->tiles;
            map.sourceEntry   = entry;
        }
    }
    return {};
}

std::expected<std::vector<MapSuitePack>, std::string> ParseMapSuitePlan(std::string_view text)
{
    std::vector<MapSuitePack> plan;
    std::set<std::string>     seen;
    std::size_t               number  = 0;
    std::size_t               section = 0;
    bool                      inMap   = false;
    const auto                failed  = [&number](std::string_view problem)
    {
        return std::unexpected("Line " + std::to_string(number) + ": " + std::string(problem));
    };
    const auto finish = [&]() -> std::string
    {
        const bool imported = inMap && seen.contains("source");
        const auto required = imported ? std::vector<std::string>{ "source" } : inMap ? std::vector<std::string>{ "land", "id", "map", "title" }
                                                                                      : std::vector<std::string>{ "id", "name", "slot" };
        for (const auto& key : required)
        {
            if (section != 0 && !seen.contains(key))
            {
                return "Line " + std::to_string(section) + ": the section has no " + key + ".";
            }
        }
        for (const auto* key : { "land", "size", "players", "mirror", "seed", "teams" })
        {
            if (imported && seen.contains(key))
            {
                return "Line " + std::to_string(section) + ": a map with a source takes no " + key + ".";
            }
        }
        if (inMap && !imported && seen.contains("randomize"))
        {
            return "Line " + std::to_string(section) + ": randomize needs a source.";
        }
        if (const auto* map = inMap ? &plan.back().maps.back() : nullptr; map != nullptr && map->teams)
        {
            const bool mirrored = map->land == StartingLand::Skirmish || (StartingLandMirrors(map->land) && map->mirror != MapMirror::Off);
            if (!mirrored || map->players % 2 != 0)
            {
                return "Line " + std::to_string(section) + ": teams needs a mirror and an even player count.";
            }
        }
        return {};
    };

    // Editors on Windows may save a UTF-8 byte order mark.
    if (text.starts_with("\xEF\xBB\xBF"))
    {
        text.remove_prefix(3);
    }
    while (!text.empty())
    {
        ++number;
        const auto end  = text.find('\n');
        const auto line = trim(text.substr(0, end));
        text            = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);
        if (line.empty() || line.front() == '#')
        {
            continue;
        }
        if (line == "[pack]" || line == "[map]")
        {
            if (auto problem = finish(); !problem.empty())
            {
                return std::unexpected(problem);
            }
            seen.clear();
            section = number;
            inMap   = line == "[map]";
            if (!inMap)
            {
                plan.push_back({ number, {}, {} });
            }
            else if (plan.empty())
            {
                return failed("a [map] comes before any [pack].");
            }
            else
            {
                plan.back().maps.emplace_back().line = number;
            }
            continue;
        }
        const auto equals = line.find('=');
        if (plan.empty() || equals == std::string_view::npos)
        {
            return failed(plan.empty() ? "expected [pack]." : "expected key = value.");
        }
        const auto key   = std::string(trim(line.substr(0, equals)));
        const auto value = trim(line.substr(equals + 1));
        if (!seen.insert(key).second)
        {
            return failed(key + " is set twice.");
        }
        if (value.empty())
        {
            return failed(key + " has no value.");
        }
        int choice = 0;
        if (!inMap)
        {
            auto& settings = plan.back().settings;
            if (key == "id")
            {
                settings.id = value;
            }
            else if (key == "name")
            {
                settings.name = value;
            }
            else if (key == "version")
            {
                settings.version = value;
            }
            else if (key == "author")
            {
                settings.author = value;
            }
            else if (key == "description")
            {
                settings.description = value;
            }
            else if (key == "slot")
            {
                if (!parseNumber(value, settings.slot))
                {
                    return failed("slot is not a number.");
                }
            }
            else
            {
                return failed("a [pack] has no key " + key + ".");
            }
            continue;
        }
        auto& map = plan.back().maps.back();
        if (key == "land")
        {
            const auto found = std::ranges::find(kStartingLands, value, &StartingLandInfo::name);
            if (found == kStartingLands.end())
            {
                return failed("no starting land is named " + std::string(value) + ".");
            }
            map.land = found->land;
        }
        else if (key == "size")
        {
            if (!parseName(value, kSizeNames, choice))
            {
                return failed("size is Small, Medium or Large.");
            }
            map.size = static_cast<LandSize>(choice);
        }
        else if (key == "mirror")
        {
            if (!parseName(value, kMirrorNames, choice))
            {
                return failed("mirror is Off, Left to Right or Top to Bottom.");
            }
            map.mirror = static_cast<MapMirror>(choice);
        }
        else if (key == "players")
        {
            if (!parseNumber(value, map.players) || map.players < kMinimumPlayers || map.players > kMaximumPlayers)
            {
                return failed("players is 2 to 7.");
            }
        }
        else if (key == "seed")
        {
            if (!parseNumber(value, map.seed))
            {
                return failed("seed is not a number.");
            }
        }
        else if (key == "id")
        {
            if (!parseNumber(value, map.id))
            {
                return failed("id is not a number.");
            }
        }
        else if (key == "map")
        {
            map.map = value;
        }
        else if (key == "title")
        {
            map.title = value;
        }
        else if (key == "description")
        {
            map.description = value;
        }
        else if (key == "teams")
        {
            if (value != "yes" && value != "no")
            {
                return failed("teams is yes or no.");
            }
            map.teams = value == "yes";
        }
        else if (key == "multiplayer")
        {
            if (value != "yes" && value != "no")
            {
                return failed("multiplayer is yes or no.");
            }
            map.multiplayer = value == "yes";
        }
        else if (key == "displaycard")
        {
            std::int16_t card = 0;
            if (!parseNumber(value, card))
            {
                return failed("displaycard is a number from -32768 to 32767.");
            }
            map.displayCard = card;
        }
        else if (key == "source")
        {
            map.source = std::filesystem::path(std::u8string(value.begin(), value.end()));
        }
        else if (key == "randomize")
        {
            std::uint32_t seed = 0;
            if (!parseNumber(value, seed))
            {
                return failed("randomize is not a number.");
            }
            map.randomize = seed;
        }
        else
        {
            return failed("a [map] has no key " + key + ".");
        }
        // The Create tab offers no other mirror for these lands.
        if (key == "mirror" || key == "land")
        {
            const auto fixed = map.land == StartingLand::Skirmish ? MapMirror::LeftToRight : MapMirror::Off;
            if (!StartingLandMirrors(map.land) && seen.contains("mirror") && map.mirror != fixed)
            {
                return failed(std::string(StartingLandOf(map.land).name) + " takes mirror = " + std::string(kMirrorNames[static_cast<std::size_t>(fixed)]) + ".");
            }
        }
    }
    if (auto problem = finish(); !problem.empty())
    {
        return std::unexpected(problem);
    }
    if (plan.empty())
    {
        return std::unexpected(std::string("The plan has no [pack]."));
    }
    for (const auto& pack : plan)
    {
        if (pack.maps.empty())
        {
            return std::unexpected("Line " + std::to_string(pack.line) + ": the pack has no [map].");
        }
    }
    return plan;
}

std::vector<std::string> MapSuiteClashes(std::span<const MapSuitePack> plan, const MapPackUsage& usage)
{
    std::vector<std::string> clashes;
    std::set<std::string>    packIds;
    std::set<std::uint32_t>  slots(usage.slots.begin(), usage.slots.end());
    std::set<std::int32_t>   ids(usage.scenarioIds.begin(), usage.scenarioIds.end());
    std::set<std::string>    mapNames;
    std::set<std::string>    usedPacks;
    for (const auto& id : usage.packIds)
    {
        usedPacks.insert(lower(id));
    }
    for (const auto& name : usage.mapNames)
    {
        mapNames.insert(lower(name));
    }
    for (const auto& pack : plan)
    {
        const auto at = "Line " + std::to_string(pack.line) + ": ";
        if (usedPacks.contains(lower(pack.settings.id)))
        {
            clashes.push_back(at + "pack ID " + pack.settings.id + " is already in the output folder or another maps folder.");
        }
        else if (!packIds.insert(lower(pack.settings.id)).second)
        {
            clashes.push_back(at + "pack ID " + pack.settings.id + " repeats.");
        }
        if (!slots.insert(pack.settings.slot).second)
        {
            clashes.push_back(at + "slot " + std::to_string(pack.settings.slot) + " repeats or is in use.");
        }
        for (const auto& map : pack.maps)
        {
            const auto mapAt = "Line " + std::to_string(map.line) + ": ";
            if (!ids.insert(map.id).second)
            {
                clashes.push_back(mapAt + "scenario ID " + std::to_string(map.id) + " repeats or is in use.");
            }
            if (!mapNames.insert(lower(map.map)).second)
            {
                clashes.push_back(mapAt + "MAP " + map.map + " repeats or is in use.");
            }
        }
    }
    return clashes;
}

std::expected<std::vector<MapPackReceipt>, std::string> ExportMapSuite(std::span<const MapSuitePack> plan, const std::filesystem::path& outputRoot, const MapPackUsage& usage, const std::filesystem::path& mapsFolder)
{
    std::error_code error;
    if (!std::filesystem::is_directory(outputRoot, error))
    {
        return std::unexpected("The output folder " + outputRoot.string() + " does not exist.");
    }
    if (!mapsFolder.empty())
    {
        if (!std::filesystem::is_directory(mapsFolder, error))
        {
            return std::unexpected("The map file folder " + mapsFolder.string() + " does not exist.");
        }
        for (const auto& pack : plan)
        {
            for (const auto& map : pack.maps)
            {
                if (std::filesystem::exists(mapFile(mapsFolder, pack, map), error) || error)
                {
                    return std::unexpected(mapFile(mapsFolder, pack, map).string() + " already exists, so nothing was written.");
                }
            }
        }
    }
    auto used = usage;
    AddMapPackUsage(used, outputRoot);
    if (!used.complete)
    {
        return std::unexpected(std::string("A maps folder could not be read in full, so clashes cannot be ruled out. Nothing was written."));
    }
    if (const auto clashes = MapSuiteClashes(plan, used); !clashes.empty())
    {
        std::string text = "The plan clashes, so nothing was written.";
        for (const auto& clash : clashes)
        {
            text += '\n' + clash;
        }
        return std::unexpected(text);
    }
    // Every pack assembles once before any is written, so a bad map writes nothing. Packs are
    // built one at a time to bound memory, and again for writing.
    std::vector<std::string> hashes;
    for (const auto& pack : plan)
    {
        const auto built    = buildPack(pack);
        const auto assembly = built ? AssembleMapPack(packMaps(*built), pack.settings) : std::expected<MapPackAssembly, std::string>(std::unexpect, built.error());
        if (!assembly)
        {
            return std::unexpected(pack.settings.id + " cannot be assembled, so nothing was written.\n" + assembly.error());
        }
        if (assembly->archive.size() > kMapSuiteMaxPackBytes)
        {
            return std::unexpected(pack.settings.id + "'s dlc.fpk is " + std::to_string(assembly->archive.size()) + " bytes, over the suite's " +
                                   std::to_string(kMapSuiteMaxPackBytes >> 20) + " MiB limit, so nothing was written. Move maps to another pack.");
        }
        hashes.push_back(assembly->sha256);
    }

    std::vector<MapPackReceipt> receipts;
    for (std::size_t index = 0; index < plan.size(); ++index)
    {
        const auto& pack  = plan[index];
        const auto  built = buildPack(pack);
        if (!built)
        {
            return std::unexpected(built.error());
        }
        auto receipt = ExportMapPack(packMaps(*built), pack.settings, outputRoot);
        if (!receipt.published || receipt.sha256 != hashes[index])
        {
            return std::unexpected(FormatMapPackReceipt(receipt) + (receipt.published ? "The pack differs from its first assembly.\n" : "") +
                                   std::to_string(index) + " earlier pack(s) were written.");
        }
        receipts.push_back(std::move(receipt));
        for (std::size_t map = 0; !mapsFolder.empty() && map < pack.maps.size(); ++map)
        {
            const auto&   one = (*built)[map];
            SavedMapDraft saved;
            saved.identity                = one.draft.identity;
            saved.synthetic               = false;
            saved.source                  = one.tiles;
            saved.tiles                   = one.tiles;
            saved.scenario                = one.draft.scenario;
            saved.exportForm.name         = pack.settings.name;
            saved.exportForm.id           = pack.settings.id;
            saved.exportForm.version      = pack.settings.version;
            saved.exportForm.author       = pack.settings.author;
            saved.exportForm.description  = pack.settings.description;
            saved.exportForm.idEdited     = true;
            saved.exportForm.slot         = static_cast<std::int32_t>(pack.settings.slot);
            saved.exportForm.outputParent = std::filesystem::absolute(outputRoot);
            saved.savedAt                 = MapDraftClockNow();
            if (const auto written = SaveMapDraft(mapFile(mapsFolder, pack, pack.maps[map]), EncodeMapDraft(saved)); !written)
            {
                return std::unexpected("The map file for line " + std::to_string(pack.maps[map].line) + " was not written. " + written.error());
            }
        }
    }
    return receipts;
}

} // namespace rerevved::tools
