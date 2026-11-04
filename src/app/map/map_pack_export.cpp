#include "map/map_pack_export.h"

#include "asset_pack_writer.h"
#include "fpk_document.h"
#include "fpk_table.h"
#include "fpk_writer.h"
#include "heights_dds.h"
#include "map/map_edit_model.h"
#include "map/scenario_form.h"
#include "map_bundle_writer.h"
#include "scenario_document.h"
#include "terrain_dds.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

namespace rerevved::tools
{
namespace
{

// Mirrors the asset pack writer's id rule.
bool validPackId(std::string_view id)
{
    if (id.empty() || id.size() > 63 || id.front() == '-' || id.back() == '-')
    {
        return false;
    }
    bool hyphen = false;
    for (const char character : id)
    {
        if (character == '-')
        {
            if (hyphen)
            {
                return false;
            }
            hyphen = true;
        }
        else
        {
            if (!(character >= 'a' && character <= 'z') && !(character >= '0' && character <= '9'))
            {
                return false;
            }
            hyphen = false;
        }
    }
    return true;
}

// Mirrors the asset pack writer's "major.minor" rule.
bool validPackVersion(std::string_view version)
{
    const auto dot = version.find('.');
    if (dot == std::string_view::npos || dot == 0 || dot + 1 == version.size())
    {
        return false;
    }
    for (std::size_t index = 0; index < version.size(); ++index)
    {
        if (index != dot && (version[index] < '0' || version[index] > '9'))
        {
            return false;
        }
    }
    return true;
}

bool validUtf8(std::string_view text)
{
    for (std::size_t index = 0; index < text.size();)
    {
        const auto  lead      = static_cast<unsigned char>(text[index++]);
        std::size_t remaining = 0;
        if (lead >= 0xC2 && lead <= 0xDF)
        {
            remaining = 1;
        }
        else if (lead >= 0xE0 && lead <= 0xEF)
        {
            remaining = 2;
        }
        else if (lead >= 0xF0 && lead <= 0xF4)
        {
            remaining = 3;
        }
        else if (lead >= 0x80)
        {
            return false;
        }
        for (; remaining > 0; --remaining)
        {
            if (index == text.size() || (static_cast<unsigned char>(text[index++]) & 0xC0U) != 0x80U)
            {
                return false;
            }
        }
    }
    return true;
}

// The writer accepts any name with a code point that is not whitespace. ASCII whitespace covers
// what users type.
bool blankText(std::string_view text)
{
    return std::ranges::all_of(text, [](char character)
                               {
                                   return character == ' ' || (character >= '\t' && character <= '\r');
                               });
}

bool containsPath(const std::filesystem::path& root, const std::filesystem::path& child)
{
    auto candidate = child.begin();
    for (const auto& component : root)
    {
        if (candidate == child.end())
        {
            return false;
        }
#ifdef _WIN32
        auto       left  = component.native();
        auto       right = candidate->native();
        const auto lower = [](wchar_t value)
        {
            return value >= L'A' && value <= L'Z' ? value + (L'a' - L'A') : value;
        };
        std::ranges::transform(left, left.begin(), lower);
        std::ranges::transform(right, right.begin(), lower);
        if (left != right)
#else
        if (component != *candidate)
#endif
        {
            return false;
        }
        ++candidate;
    }
    return true;
}

std::string mapLetter(const MapDraft& draft)
{
    return draft.scenario && !draft.scenario->entries.empty() ? draft.scenario->entries.front().map : std::string();
}

std::string slotFileName(std::uint32_t slot)
{
    return "DLCScenarioData" + std::to_string(slot) + ".xml";
}

std::string lower(std::string text)
{
    std::ranges::transform(text, text.begin(), [](char character)
                           {
                               return character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a') : character;
                           });
    return text;
}

std::string lowerName(const std::filesystem::path& path)
{
    const auto utf8 = path.filename().generic_u8string();
    return lower(std::string(utf8.begin(), utf8.end()));
}

// Empty for a dlc.fpk outside a pack's assets/map-pack folder.
std::filesystem::path packFolder(const std::filesystem::path& path)
{
    std::error_code error;
    auto            normal = std::filesystem::absolute(path, error).lexically_normal();
    if (!normal.has_filename())
    {
        normal = normal.parent_path();
    }
    const auto name = lowerName(normal);
    if (name == "asset-pack.toml")
    {
        return normal.parent_path();
    }
    if (name == "dlc.fpk")
    {
        const auto mapPack = normal.parent_path();
        const auto assets  = mapPack.parent_path();
        return lowerName(mapPack) == "map-pack" && lowerName(assets) == "assets" ? assets.parent_path() : std::filesystem::path();
    }
    return normal;
}

bool overlapsProtected(const std::filesystem::path& packDirectory, std::span<const std::filesystem::path> protectedRoots)
{
    std::error_code error;
    const auto      destination = std::filesystem::weakly_canonical(packDirectory, error);
    if (error)
    {
        return true;
    }
    for (const auto& other : protectedRoots)
    {
        if (other.empty())
        {
            continue;
        }
        const auto resolved = std::filesystem::weakly_canonical(other, error);
        if (error || containsPath(resolved, destination) || containsPath(destination, resolved))
        {
            return true;
        }
    }
    return false;
}

} // namespace

namespace
{

std::string rowIssue(const ScenarioIssue& issue)
{
    const std::string_view field = issue.field;
    const std::string_view row   = field.starts_with("TITLE/") ? "Title" : field.starts_with("DESCRIPTION/") ? "Description"
                                                                       : field == "ID"                       ? "Scenario ID"
                                                                       : field == "MAP"                      ? "Map Letter"
                                                                                                             : field;
    return row.empty() ? std::string(ScenarioErrorMessage(issue.code)) : std::string(row) + ": " + std::string(ScenarioErrorMessage(issue.code));
}

} // namespace

std::vector<std::string> MapPackBlockers(const MapDraft& draft, const MapDocument* map, const MapPackSettings& settings)
{
    std::vector<std::string> blockers;
    if (!draft.started)
    {
        blockers.emplace_back("Start a map first.");
        return blockers;
    }
    if (map == nullptr)
    {
        blockers.emplace_back("Open or create the tile grid.");
    }
    // Two players are the fewest a game has. Each needs a marker to start on.
    else if (MapStartMarkerCount(*map) < 2)
    {
        blockers.emplace_back("Place at least 2 start markers on the Grid.");
    }
    if (!draft.scenario || draft.scenario->entries.empty())
    {
        blockers.emplace_back("The map has no scenario entry.");
    }
    else
    {
        // Text in every language shares one row, so its problem is listed once.
        for (const auto& issue : ValidateScenarioDocument(*draft.scenario))
        {
            if (auto text = rowIssue(issue); !std::ranges::contains(blockers, text))
            {
                blockers.push_back(std::move(text));
            }
        }
        for (auto& blocker : ScenarioMapBindingBlockers(*draft.scenario))
        {
            blockers.push_back(std::move(blocker));
        }
    }
    // Generation runs in the background, so images can be missing or trail the grid for a moment.
    const auto missing = [map](const auto& image)
    {
        return !image || (map != nullptr && image->generatedFrom != map->core);
    };
    const auto unread = [](const auto& image)
    {
        return image && !image->readBackPassed;
    };
    if (missing(draft.heights) || std::ranges::any_of(draft.terrain, missing))
    {
        blockers.emplace_back("Generating map images...");
    }
    if (unread(draft.heights) || std::ranges::any_of(draft.terrain, unread))
    {
        blockers.emplace_back("A map image failed its read back check.");
    }
    if (settings.id.empty())
    {
        blockers.emplace_back("Pack ID: enter one.");
    }
    else if (!validPackId(settings.id))
    {
        blockers.emplace_back("Pack ID: use lowercase letters, digits and single dashes.");
    }
    else if (settings.id.size() > kMapPackMaxIdLength)
    {
        blockers.emplace_back("Pack ID: 39 characters at most.");
    }
    // An empty title already has its own row problem.
    if (blankText(settings.name) && !std::ranges::any_of(blockers, [](const std::string& blocker)
                                                         {
                                                             return blocker.starts_with("Title:");
                                                         }))
    {
        blockers.emplace_back("Title: enter one.");
    }
    if (!validPackVersion(settings.version))
    {
        blockers.emplace_back("Version: major.minor, for example 1.0.");
    }
    if (settings.slot < kMapPackFirstSlot || settings.slot > kMapPackLastSlot)
    {
        blockers.emplace_back("Scenario File Slot: 5 to 31.");
    }
    if (!validUtf8(settings.name) || !validUtf8(settings.author) || !validUtf8(settings.description))
    {
        blockers.emplace_back("Title, author and description must be valid UTF-8 text.");
    }
    return blockers;
}

std::expected<MapPackAssembly, std::string> AssembleMapPack(const MapDraft& draft, const MapDocument& map, const MapPackSettings& settings)
{
    const MapPackMap one{ &draft, &map };
    return AssembleMapPack(std::span(&one, 1), settings);
}

std::expected<MapPackAssembly, std::string> AssembleMapPack(std::span<const MapPackMap> maps, const MapPackSettings& settings)
{
    if (maps.empty())
    {
        return std::unexpected(std::string("The pack has no map."));
    }
    ScenarioDocument scenario;
    for (const auto& [draft, map] : maps)
    {
        if (const auto blockers = MapPackBlockers(*draft, map, settings); !blockers.empty())
        {
            return std::unexpected(maps.size() > 1 ? draft->identity + ": " + blockers.front() : blockers.front());
        }
        std::ranges::copy(draft->scenario->entries, std::back_inserter(scenario.entries));
    }
    // Maps in one pack must not share a MAP name or scenario ID.
    if (const auto issues = ValidateScenarioDocument(scenario); !issues.empty())
    {
        return std::unexpected(rowIssue(issues.front()));
    }

    auto xml = EncodeScenarioXml(scenario);
    if (!xml)
    {
        return std::unexpected("Scenario XML failed: " + FormatScenarioIssue(xml.error()));
    }
    const auto parsedScenario = ParseScenarioDocument(*xml);
    if (!parsedScenario || *parsedScenario != scenario)
    {
        return std::unexpected("Scenario XML read back did not match.");
    }

    MapPackAssembly assembly;
    FpkArchive      archive;
    archive.entries.push_back({ slotFileName(settings.slot), std::move(*xml) });
    for (const auto& [draft, map] : maps)
    {
        const auto failed = [&, draft](std::string text)
        {
            return std::unexpected(maps.size() > 1 ? draft->identity + ": " + text : text);
        };
        // The footer proved in the trial is all 0xFF, not the document's footer.
        std::vector<std::byte> mapBytes(map->core.begin(), map->core.end());
        mapBytes.resize(kMapEnvelopeSize, std::byte{ 0xFF });
        const auto parsedMap = ParseMapDocument(mapBytes);
        if (!parsedMap || parsedMap->core != map->core ||
            !std::ranges::all_of(parsedMap->footer, [](std::byte value)
                                 {
                                     return value == std::byte{ 0xFF };
                                 }))
        {
            return failed("MAP read back did not match.");
        }

        auto heights = EncodeHeightsDds(draft->heights->samples);
        if (!heights)
        {
            return failed("Heights image failed: " + std::string(HeightsDdsErrorMessage(heights.error())));
        }
        const auto parsedHeights = ParseHeightsDds(*heights);
        if (!parsedHeights || *parsedHeights != draft->heights->samples)
        {
            return failed("Heights image read back did not match.");
        }

        const auto& blends   = *draft->terrain[static_cast<std::size_t>(TerrainTexture::MountainHillBlends)];
        const auto& lightmap = *draft->terrain[static_cast<std::size_t>(TerrainTexture::Lightmap)];
        if (!ParseTerrainDds(TerrainTexture::MountainHillBlends, blends.dds))
        {
            return failed("Mountain and hill blends image is not a valid blends DDS.");
        }
        if (!ParseTerrainDds(TerrainTexture::Lightmap, lightmap.dds))
        {
            return failed("Lightmap image is not a valid lightmap DDS.");
        }

        const std::string name = mapLetter(*draft);
        assembly.mapName += (assembly.mapName.empty() ? "" : ", ") + name;
        archive.entries.push_back({ name + ".map", std::move(mapBytes) });
        archive.entries.push_back({ "Map" + name + "_Heights.dds", std::move(*heights) });
        archive.entries.push_back({ "Map" + name + "_MountainHill_Blends.dds", blends.dds });
        archive.entries.push_back({ "Map" + name + "_Lightmap.dds", lightmap.dds });
    }

    auto encoded = EncodeFpkArchive(archive);
    if (!encoded)
    {
        return std::unexpected("Archive failed: " + std::string(FpkWriteErrorMessage(encoded.error())));
    }
    if (encoded->size() > kMapPackMaxArchiveBytes)
    {
        return std::unexpected("dlc.fpk is " + std::to_string(encoded->size() >> 20) + " MiB. ReRevved loads 256 MiB at most, so split the maps across packs.");
    }
    const auto parsed = ParseFpkDocument(*encoded);
    if (!parsed || parsed->index.entries.size() != archive.entries.size())
    {
        return std::unexpected("Archive read back failed.");
    }
    for (std::size_t index = 0; index < archive.entries.size(); ++index)
    {
        const auto& entry = parsed->index.entries[index];
        if (entry.offset > encoded->size() || entry.size > encoded->size() - entry.offset ||
            !std::ranges::equal(std::span<const std::byte>(*encoded).subspan(entry.offset, entry.size), archive.entries[index].bytes))
        {
            return std::unexpected("Archive entry " + archive.entries[index].name + " did not read back.");
        }
        assembly.entryNames.push_back(archive.entries[index].name);
    }
    assembly.sha256  = CalculateMapBundleSha256(*encoded);
    assembly.archive = std::move(*encoded);
    return assembly;
}

MapPackReceipt ExportMapPack(const MapDraft&                        draft,
                             const MapDocument&                     map,
                             const MapPackSettings&                 settings,
                             const std::filesystem::path&           mapsRoot,
                             std::span<const std::filesystem::path> protectedRoots)
{
    const MapPackMap one{ &draft, &map };
    return ExportMapPack(std::span(&one, 1), settings, mapsRoot, protectedRoots);
}

MapPackReceipt ExportMapPack(std::span<const MapPackMap>            maps,
                             const MapPackSettings&                 settings,
                             const std::filesystem::path&           mapsRoot,
                             std::span<const std::filesystem::path> protectedRoots)
{
    MapPackReceipt receipt;
    if (maps.size() == 1)
    {
        receipt.revision = maps.front().draft->revision;
        receipt.identity = maps.front().draft->identity;
    }
    else
    {
        receipt.identity = settings.name;
    }
    receipt.slot          = settings.slot;
    receipt.packDirectory = mapsRoot / settings.id;

    auto assembly = AssembleMapPack(maps, settings);
    if (!assembly)
    {
        receipt.diagnostic = assembly.error();
        return receipt;
    }
    receipt.mapName      = assembly->mapName;
    receipt.archiveBytes = assembly->archive.size();
    receipt.sha256       = assembly->sha256;

    if (overlapsProtected(receipt.packDirectory, protectedRoots))
    {
        receipt.diagnostic =
            "The pack folder overlaps a game, source or reference folder. Choose another folder.";
        return receipt;
    }

    AssetPackDescription description;
    description.id      = settings.id;
    description.name    = settings.name;
    description.version = settings.version;
    if (!settings.author.empty())
    {
        description.author = settings.author;
    }
    if (!settings.description.empty())
    {
        description.description = settings.description;
    }
    description.assets.push_back({ std::string(kMapPackAssetKey), assembly->archive });
    description.mapPack = MapPackInfo{ settings.slot, {} };
    for (const auto& one : maps)
    {
        for (const auto& entry : one.draft->scenario->entries)
        {
            const auto named = std::ranges::find_if(entry.title, [](const std::string& text)
                                                    {
                                                        return !text.empty();
                                                    });
            description.mapPack->scenarios.push_back(
                { entry.id, named != entry.title.end() ? *named : "Scenario " + std::to_string(entry.id) });
        }
    }
    if (const auto written = WriteAssetPack(description, mapsRoot); !written)
    {
        receipt.diagnostic = "Pack not written: " + std::string(AssetPackErrorMessage(written.error()));
        return receipt;
    }

    for (const auto& violation : ValidateAssetPack(receipt.packDirectory))
    {
        if (!violation.warning)
        {
            receipt.diagnostic = "Pack written but failed validation: " + std::string(AssetPackErrorMessage(violation.error)) +
                                 ".\nDelete the " + settings.id + " folder before exporting again.";
            return receipt;
        }
    }
    std::ifstream     input(receipt.packDirectory / "assets" / "map-pack" / "dlc.fpk", std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (bytes.size() != assembly->archive.size() ||
        !std::equal(bytes.begin(), bytes.end(), reinterpret_cast<const char*>(assembly->archive.data())))
    {
        receipt.diagnostic =
            "Pack written but dlc.fpk did not read back byte for byte.\nDelete the " +
            settings.id + " folder before exporting again.";
        return receipt;
    }
    receipt.published  = true;
    receipt.diagnostic = "Map pack written and read back.\nCopy the " + settings.id +
                         " folder into ReRevved's maps folder, add its ID to map_order.txt, then restart the game.";
    return receipt;
}

std::expected<SavedMapDraft, std::string> LoadMapPack(const std::filesystem::path& path)
{
    const auto folder = packFolder(path);
    if (folder.empty())
    {
        return std::unexpected(std::string("A map pack's dlc.fpk sits in its assets/map-pack folder. This one does not."));
    }
    const auto manifest = ReadAssetPackManifest(folder);
    if (!manifest)
    {
        return std::unexpected("It is not a map pack Tools can read. " + std::string(AssetPackErrorMessage(manifest.error())));
    }
    if (!manifest->mapPack)
    {
        return std::unexpected(std::string("Its asset-pack.toml has no [map_pack] table, so it holds no map."));
    }
    const auto slot  = manifest->mapPack->slot;
    const auto table = ReadFpkTable(folder / "assets" / "map-pack" / "dlc.fpk", 64);
    if (!table)
    {
        return std::unexpected("Its dlc.fpk could not be read. " + table.error());
    }
    const FpkTableEntry* mapEntry = nullptr;
    const FpkTableEntry* xmlEntry = nullptr;
    for (const auto& entry : *table)
    {
        const auto name = lower(entry.name);
        if (name.ends_with(".map"))
        {
            if (mapEntry != nullptr)
            {
                return std::unexpected(std::string("Its dlc.fpk holds more than one map."));
            }
            mapEntry = &entry;
        }
        else if (name == lower(slotFileName(slot)))
        {
            xmlEntry = &entry;
        }
    }
    if (mapEntry == nullptr)
    {
        return std::unexpected(std::string("Its dlc.fpk holds no map."));
    }
    if (xmlEntry == nullptr)
    {
        return std::unexpected("Its dlc.fpk has no " + slotFileName(slot) + " for scenario file slot " + std::to_string(slot) + ".");
    }
    const auto mapBytes = ReadFpkTableEntry(*mapEntry, kMapEnvelopeSize);
    if (!mapBytes)
    {
        return std::unexpected(mapEntry->name + " could not be read. " + mapBytes.error());
    }
    const auto map = ParseMapDocument(*mapBytes);
    if (!map)
    {
        return std::unexpected(mapEntry->name + " is not a valid map. " + std::string(MapDocumentErrorMessage(map.error())));
    }
    const auto xmlBytes = ReadFpkTableEntry(*xmlEntry, kScenarioMaximumXmlBytes);
    if (!xmlBytes)
    {
        return std::unexpected(xmlEntry->name + " could not be read. " + xmlBytes.error());
    }
    auto scenario = ParseScenarioDocument(*xmlBytes);
    if (!scenario)
    {
        return std::unexpected(xmlEntry->name + " is not valid. " + FormatScenarioIssue(scenario.error()));
    }

    SavedMapDraft draft;
    draft.synthetic = false;
    draft.source    = *map;
    draft.tiles     = *map;
    // The map's own scenario title names it, as the wizard does.
    const auto letter = mapEntry->name.substr(0, mapEntry->name.size() - 4);
    for (const auto& entry : scenario->entries)
    {
        const auto named = std::ranges::find_if(entry.title, [](const std::string& text)
                                                {
                                                    return !text.empty();
                                                });
        if (lower(entry.map) == lower(letter) && named != entry.title.end())
        {
            draft.identity = *named;
            break;
        }
    }
    if (draft.identity.empty())
    {
        draft.identity = manifest->name;
    }
    draft.scenario                = std::move(*scenario);
    draft.exportForm.name         = manifest->name;
    draft.exportForm.id           = manifest->id;
    draft.exportForm.version      = manifest->version;
    draft.exportForm.author       = manifest->author.value_or(std::string());
    draft.exportForm.description  = manifest->description.value_or(std::string());
    draft.exportForm.idEdited     = true;
    draft.exportForm.slot         = static_cast<std::int32_t>(slot);
    draft.exportForm.outputParent = folder.parent_path();
    return draft;
}

std::string FormatMapPackReceipt(const MapPackReceipt& receipt)
{
    std::string text = receipt.published ? "Map pack: written\n" : "Map pack: not written\n";
    text += "Draft: " + receipt.identity + " (revision " + std::to_string(receipt.revision) + ")\n";
    if (!receipt.packDirectory.empty())
    {
        text += "Pack folder: " + receipt.packDirectory.string() + '\n';
    }
    if (!receipt.mapName.empty())
    {
        text += "MAP: " + receipt.mapName + '\n';
        text += "Scenario file slot: " + std::to_string(receipt.slot) + '\n';
        text += "dlc.fpk: " + std::to_string(receipt.archiveBytes) + " bytes\n";
        text += "SHA-256: " + receipt.sha256 + '\n';
    }
    text += receipt.diagnostic + '\n';
    return text;
}

} // namespace rerevved::tools
