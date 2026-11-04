#include "map/map_draft_store.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <format>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

namespace rerevved::tools
{
namespace
{

constexpr std::string_view kHeader = "ReRevved Tools map draft 1";
// A draft is a few kilobytes. Anything far larger is not one.
constexpr std::size_t kMaximumBytes = 1U << 20;
// Saves after 9999-12-31 are damage, and the limit keeps age arithmetic in range.
constexpr std::int64_t kLatestSave = 253402300799;
// Fields allowed once. All are required except scenario and file.
constexpr std::array<std::string_view, 17> kSingle{ "saved", "identity", "file", "step", "origin", "source", "tiles", "scenario", "export-name", "export-id", "export-version", "export-author", "export-description", "export-slot", "export-save-to", "export-id-edited", "export-name-prefilled" };

constexpr std::string_view kRecentHeader = "ReRevved Tools recent maps 1";

constexpr std::array<std::pair<MapStep, std::string_view>, 7> kSteps{ {
    { MapStep::EditGrid, "grid" },
    { MapStep::Heights, "heights" },
    { MapStep::MountainHillBlends, "blends" },
    { MapStep::Lightmap, "lightmap" },
    { MapStep::Scenario, "scenario" },
    { MapStep::Review, "review" },
    { MapStep::Export, "export" },
} };

std::string utf8(const std::filesystem::path& path)
{
    const auto text = path.u8string();
    return { reinterpret_cast<const char*>(text.data()), text.size() };
}

// Damaged UTF-8 throws on Windows path conversion. It becomes a refusal here.
std::optional<std::filesystem::path> fromUtf8(std::string_view text)
{
    try
    {
        return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
    }
    catch (const std::exception&)
    {
        return std::nullopt;
    }
}

// Control characters, DEL and '%' become %XX so every value stays on one line.
std::string escape(std::string_view text)
{
    std::string result;
    for (const char character : text)
    {
        const auto value = static_cast<unsigned char>(character);
        if (value < 0x20 || value == 0x7F || value == '%')
        {
            result += std::format("%{:02X}", value);
        }
        else
        {
            result += character;
        }
    }
    return result;
}

std::optional<std::string> unescape(std::string_view text)
{
    std::string result;
    for (std::size_t index = 0; index < text.size(); ++index)
    {
        if (text[index] != '%')
        {
            result += text[index];
            continue;
        }
        unsigned int value = 0;
        if (index + 2 >= text.size() || std::from_chars(text.data() + index + 1, text.data() + index + 3, value, 16).ptr != text.data() + index + 3)
        {
            return std::nullopt;
        }
        result += static_cast<char>(value);
        index += 2;
    }
    return result;
}

template <std::size_t N>
std::string hex(const std::array<std::byte, N>& bytes)
{
    std::string text;
    text.reserve(N * 2);
    for (const auto value : bytes)
    {
        text += std::format("{:02x}", std::to_integer<unsigned int>(value));
    }
    return text;
}

template <std::size_t N>
bool unhex(std::string_view text, std::array<std::byte, N>& bytes)
{
    if (text.size() != N * 2)
    {
        return false;
    }
    for (std::size_t index = 0; index < N; ++index)
    {
        unsigned int value = 0;
        if (std::from_chars(text.data() + index * 2, text.data() + index * 2 + 2, value, 16).ptr != text.data() + index * 2 + 2)
        {
            return false;
        }
        bytes[index] = static_cast<std::byte>(value);
    }
    return true;
}

template <typename T>
bool number(std::string_view text, T& value)
{
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size();
}

std::vector<std::string_view> words(std::string_view text)
{
    std::vector<std::string_view> result;
    while (!text.empty())
    {
        const auto space = text.find(' ');
        result.push_back(text.substr(0, space));
        if (space == std::string_view::npos)
        {
            break;
        }
        text.remove_prefix(space + 1);
    }
    return result;
}

void line(std::string& text, std::string_view key, std::string_view value)
{
    text += key;
    if (!value.empty())
    {
        text += ' ';
        text += value;
    }
    text += '\n';
}

} // namespace

bool operator==(const SavedMapDraft& left, const SavedMapDraft& right)
{
    const auto same = [](const MapDocument& a, const MapDocument& b)
    {
        return a.core == b.core && a.footer == b.footer;
    };
    return left.identity == right.identity && left.file == right.file && left.step == right.step && left.synthetic == right.synthetic && same(left.source, right.source) && same(left.tiles, right.tiles) &&
           left.scenario == right.scenario && left.exportForm == right.exportForm && left.savedAt == right.savedAt;
}

std::string EncodeMapDraft(const SavedMapDraft& draft)
{
    std::string text(kHeader);
    text += '\n';
    line(text, "saved", std::to_string(draft.savedAt));
    line(text, "identity", escape(draft.identity));
    if (!draft.file.empty())
    {
        line(text, "file", escape(utf8(draft.file)));
    }
    const auto step = std::ranges::find(kSteps, draft.step, &std::pair<MapStep, std::string_view>::first);
    line(text, "step", step != kSteps.end() ? step->second : "grid");
    line(text, "origin", draft.synthetic ? "new" : "copy");
    line(text, "source", hex(draft.source.core) + hex(draft.source.footer));
    line(text, "tiles", hex(draft.tiles.core) + hex(draft.tiles.footer));
    if (draft.scenario)
    {
        line(text, "scenario", {});
        for (const auto& entry : draft.scenario->entries)
        {
            line(text, "entry", {});
            line(text, "map", escape(entry.map));
            line(text, "multiplayer", entry.multiplayer ? "1" : "0");
            line(text, "id", std::to_string(entry.id));
            for (std::size_t locale = 0; locale < entry.title.size(); ++locale)
            {
                if (!entry.title[locale].empty())
                {
                    line(text, "title", std::to_string(locale) + ' ' + escape(entry.title[locale]));
                }
                if (!entry.description[locale].empty())
                {
                    line(text, "description", std::to_string(locale) + ' ' + escape(entry.description[locale]));
                }
            }
            std::string levels;
            for (const auto level : entry.variators)
            {
                levels += (levels.empty() ? "" : " ") + std::to_string(level);
            }
            line(text, "variators", levels);
        }
    }
    const auto& form = draft.exportForm;
    line(text, "export-name", escape(form.name));
    line(text, "export-id", escape(form.id));
    line(text, "export-version", escape(form.version));
    line(text, "export-author", escape(form.author));
    line(text, "export-description", escape(form.description));
    line(text, "export-id-edited", form.idEdited ? "1" : "0");
    line(text, "export-name-prefilled", form.namePrefilled ? "1" : "0");
    line(text, "export-slot", std::to_string(form.slot));
    line(text, "export-save-to", escape(utf8(form.outputParent)));
    line(text, "end", {});
    return text;
}

std::expected<SavedMapDraft, std::string> DecodeMapDraft(std::string_view text)
{
    SavedMapDraft                 draft;
    ScenarioEntry*                entry  = nullptr;
    bool                          header = false;
    bool                          ended  = false;
    std::vector<std::string_view> seen;
    // Title and description carry their locale.
    std::vector<std::string> entrySeen;
    const auto               entryComplete = [&]
    {
        return entry == nullptr || std::ranges::all_of(std::array<std::string_view, 4>{ "map", "multiplayer", "id", "variators" }, [&](std::string_view key)
                                                       {
                                                           return std::ranges::contains(entrySeen, key);
                                                       });
    };
    std::size_t lineNumber = 0;
    const auto  fail       = [&](std::string_view why)
    {
        return std::unexpected("Line " + std::to_string(lineNumber) + ": " + std::string(why));
    };
    while (!text.empty())
    {
        ++lineNumber;
        const auto newline = text.find('\n');
        auto       current = text.substr(0, newline);
        text.remove_prefix(newline == std::string_view::npos ? text.size() : newline + 1);
        if (current.ends_with('\r'))
        {
            current.remove_suffix(1);
        }
        if (!header)
        {
            if (current != kHeader)
            {
                return std::unexpected(std::string("This is not a map file from this version of ReRevved Tools."));
            }
            header = true;
            continue;
        }
        if (ended)
        {
            return fail("text after the end");
        }
        const auto space = current.find(' ');
        const auto key   = current.substr(0, space);
        const auto value = space == std::string_view::npos ? std::string_view{} : current.substr(space + 1);
        const auto plain = unescape(value);
        if (!plain)
        {
            return fail("bad escape");
        }
        if (std::ranges::contains(kSingle, key))
        {
            if (std::ranges::contains(seen, key))
            {
                return fail("repeated field");
            }
            seen.push_back(key);
        }
        if (key == "saved")
        {
            if (!number(value, draft.savedAt) || draft.savedAt < 0 || draft.savedAt > kLatestSave)
            {
                return fail("bad time");
            }
        }
        else if (key == "identity")
        {
            draft.identity = *plain;
        }
        else if (key == "file")
        {
            auto path = fromUtf8(*plain);
            if (!path || path->empty())
            {
                return fail("bad file");
            }
            draft.file = std::move(*path);
        }
        else if (key == "step")
        {
            const auto step = std::ranges::find(kSteps, value, &std::pair<MapStep, std::string_view>::second);
            if (step == kSteps.end())
            {
                return fail("unknown step");
            }
            draft.step = step->first;
        }
        else if (key == "origin")
        {
            if (value != "new" && value != "copy")
            {
                return fail("unknown origin");
            }
            draft.synthetic = value == "new";
        }
        else if (key == "source" || key == "tiles")
        {
            auto& document = key == "source" ? draft.source : draft.tiles;
            if (value.size() != kMapEnvelopeSize * 2 || !unhex(value.substr(0, kMapCoreSize * 2), document.core) || !unhex(value.substr(kMapCoreSize * 2), document.footer))
            {
                return fail("bad tile bytes");
            }
        }
        else if (key == "image" || key == "stamp")
        {
            // Ignore image instructions because images are generated from tiles.
        }
        else if (key == "scenario")
        {
            if (draft.scenario)
            {
                return fail("second scenario");
            }
            draft.scenario.emplace();
        }
        else if (key == "entry")
        {
            if (!draft.scenario)
            {
                return fail("entry outside a scenario");
            }
            if (!entryComplete())
            {
                return fail("incomplete entry");
            }
            entry = &draft.scenario->entries.emplace_back();
            entrySeen.clear();
        }
        else if (key == "map" || key == "multiplayer" || key == "id" || key == "title" || key == "description" || key == "variators")
        {
            if (entry == nullptr)
            {
                return fail("field outside an entry");
            }
            const auto split = value.find(' ');
            auto       field = std::string(key);
            if (key == "title" || key == "description")
            {
                // Compare parsed locale indices so "0" and "00" count as the same one.
                std::size_t locale = 0;
                if (!number(value.substr(0, split), locale))
                {
                    return fail("bad text");
                }
                field += ' ' + std::to_string(locale);
            }
            if (std::ranges::contains(entrySeen, field))
            {
                return fail("repeated field");
            }
            entrySeen.push_back(std::move(field));
            if (key == "map")
            {
                entry->map = *plain;
            }
            else if (key == "multiplayer")
            {
                if (value != "0" && value != "1")
                {
                    return fail("bad flag");
                }
                entry->multiplayer = value == "1";
            }
            else if (key == "id")
            {
                if (!number(value, entry->id))
                {
                    return fail("bad id");
                }
            }
            else if (key == "variators")
            {
                const auto parts = words(value);
                if (parts.size() != entry->variators.size())
                {
                    return fail("bad variators");
                }
                for (std::size_t index = 0; index < parts.size(); ++index)
                {
                    if (!number(parts[index], entry->variators[index]))
                    {
                        return fail("bad variator");
                    }
                }
            }
            else
            {
                std::size_t locale = 0;
                if (split == std::string_view::npos || !number(value.substr(0, split), locale) || locale >= entry->title.size())
                {
                    return fail("bad text");
                }
                const auto literal = unescape(value.substr(split + 1));
                if (!literal)
                {
                    return fail("bad escape");
                }
                (key == "title" ? entry->title : entry->description)[locale] = *literal;
            }
        }
        else if (key == "export-name")
        {
            draft.exportForm.name = *plain;
        }
        else if (key == "export-id")
        {
            draft.exportForm.id = *plain;
        }
        else if (key == "export-version")
        {
            draft.exportForm.version = *plain;
        }
        else if (key == "export-author")
        {
            draft.exportForm.author = *plain;
        }
        else if (key == "export-description")
        {
            draft.exportForm.description = *plain;
        }
        else if (key == "export-id-edited" || key == "export-name-prefilled")
        {
            if (value != "0" && value != "1")
            {
                return fail("bad flag");
            }
            (key == "export-id-edited" ? draft.exportForm.idEdited : draft.exportForm.namePrefilled) = value == "1";
        }
        else if (key == "export-slot")
        {
            if (!number(value, draft.exportForm.slot))
            {
                return fail("bad slot");
            }
        }
        else if (key == "export-save-to")
        {
            auto path = fromUtf8(*plain);
            if (!path)
            {
                return fail("bad folder");
            }
            draft.exportForm.outputParent = std::move(*path);
        }
        else if (key == "end")
        {
            ended = true;
        }
        else
        {
            return fail("unknown field");
        }
    }
    if (!header || !ended || !entryComplete() || !std::ranges::all_of(kSingle, [&](std::string_view key)
                                                                      {
                                                                          return key == "scenario" || key == "file" || std::ranges::contains(seen, key);
                                                                      }))
    {
        return std::unexpected(std::string("The map file is incomplete."));
    }
    return draft;
}

std::expected<SavedMapDraft, std::string> LoadMapDraft(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        return std::unexpected("Could not open " + utf8(path.filename()) + ".");
    }
    std::string text;
    text.resize(kMaximumBytes);
    input.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (input.bad() || input.gcount() == static_cast<std::streamsize>(text.size()))
    {
        return std::unexpected("Could not read " + utf8(path.filename()) + ".");
    }
    text.resize(static_cast<std::size_t>(input.gcount()));
    return DecodeMapDraft(text);
}

namespace
{

std::expected<void, std::string> writeAtomically(const std::filesystem::path& path, std::string_view encoded)
{
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error)
    {
        return std::unexpected("Could not create the folder: " + error.message());
    }
    auto temporary = path;
    temporary += ".tmp";
    for (int suffix = 2; std::filesystem::exists(temporary, error) && suffix < 1000; ++suffix)
    {
        temporary = path;
        temporary += ".tmp" + std::to_string(suffix);
    }
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output || !output.write(encoded.data(), static_cast<std::streamsize>(encoded.size())) || !output.flush())
        {
            return std::unexpected("Could not write " + utf8(temporary.filename()) + ".");
        }
    }
    std::filesystem::rename(temporary, path, error);
    if (error)
    {
        std::filesystem::remove(temporary, error);
        return std::unexpected("Could not save " + utf8(path.filename()) + ".");
    }
    return {};
}

} // namespace

std::expected<void, std::string> SaveMapDraft(const std::filesystem::path& path, std::string_view encoded)
{
    // The loader refuses this size, so writing it would replace a readable map with an unreadable one.
    if (encoded.size() >= kMaximumBytes)
    {
        return std::unexpected(
            std::string("The map is too large to save. Shorten the scenario text."));
    }
    return writeAtomically(path, encoded);
}

std::vector<MapDraftListing> ListMapDrafts(const std::filesystem::path& folder)
{
    std::vector<MapDraftListing> drafts;
    std::error_code              error;
    for (std::filesystem::directory_iterator it(folder, error), end; !error && it != end; it.increment(error))
    {
        std::error_code kind;
        if (it->path().extension() != kMapDraftExtension || !it->is_regular_file(kind))
        {
            continue;
        }
        MapDraftListing listing;
        listing.path = it->path();
        if (auto draft = LoadMapDraft(listing.path))
        {
            listing.draft = std::move(*draft);
        }
        else
        {
            listing.error = draft.error();
        }
        drafts.push_back(std::move(listing));
    }
    std::ranges::sort(drafts, [](const MapDraftListing& left, const MapDraftListing& right)
                      {
                          if (left.error.empty() != right.error.empty())
                          {
                              return left.error.empty();
                          }
                          return left.draft.savedAt != right.draft.savedAt ? left.draft.savedAt > right.draft.savedAt : left.path < right.path;
                      });
    return drafts;
}

std::vector<MapDraftListing> ListMapFiles(std::span<const std::filesystem::path> files)
{
    std::vector<MapDraftListing> listings;
    for (const auto& file : files)
    {
        // A map moved or on a drive that is not connected is left out, not reported.
        std::error_code error;
        const auto      status = std::filesystem::status(file, error);
        if (status.type() == std::filesystem::file_type::not_found)
        {
            continue;
        }
        MapDraftListing listing;
        listing.path = file;
        if (error || !std::filesystem::is_regular_file(status))
        {
            listing.error = "Could not check " + utf8(file.filename()) + (error ? ": " + error.message() : std::string(": it is not a file."));
        }
        else if (auto draft = LoadMapDraft(file))
        {
            listing.draft = std::move(*draft);
        }
        else
        {
            listing.error = draft.error();
        }
        listings.push_back(std::move(listing));
    }
    return listings;
}

std::filesystem::path NewMapDraftPath(const std::filesystem::path& folder, std::int64_t now)
{
    const auto stamp = std::format("map-{:%Y%m%d-%H%M%S}", std::chrono::sys_seconds(std::chrono::seconds(now)));
    for (int suffix = 1; suffix <= 1000; ++suffix)
    {
        auto            path = folder / (stamp + (suffix == 1 ? std::string() : "-" + std::to_string(suffix)) + std::string(kMapDraftExtension));
        std::error_code error;
        const bool      taken = std::filesystem::exists(path, error);
        if (error)
        {
            return {};
        }
        if (!taken)
        {
            return path;
        }
    }
    return {};
}

std::filesystem::path DefaultAppDataFolder()
{
#ifdef _WIN32
    wchar_t*    value  = nullptr;
    std::size_t length = 0;
    if (_wdupenv_s(&value, &length, L"LOCALAPPDATA") != 0 || value == nullptr)
    {
        return {};
    }
    std::filesystem::path base(value);
    std::free(value);
    return base.empty() ? base : base / L"ReRevved Tools";
#else
    if (const char* data = std::getenv("XDG_DATA_HOME"); data != nullptr && *data == '/')
    {
        return std::filesystem::path(data) / "rerevved-tools";
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && *home == '/')
    {
        return std::filesystem::path(home) / ".local" / "share" / "rerevved-tools";
    }
    return {};
#endif
}

std::filesystem::path MapRecoveryFolder(const std::filesystem::path& dataFolder)
{
    return dataFolder.empty() ? dataFolder : dataFolder / "Recovery";
}

std::filesystem::path RecentMapsFile(const std::filesystem::path& dataFolder)
{
    return dataFolder.empty() ? dataFolder : dataFolder / "recent-maps.txt";
}

std::vector<std::filesystem::path> LoadRecentMaps(const std::filesystem::path& file)
{
    std::vector<std::filesystem::path> maps;
    std::ifstream                      input(file, std::ios::binary);
    std::string                        current;
    if (!input || !std::getline(input, current) || current != kRecentHeader)
    {
        return maps;
    }
    while (std::getline(input, current) && maps.size() < kRecentMapLimit)
    {
        const auto plain = unescape(current);
        auto       path  = plain ? fromUtf8(*plain) : std::nullopt;
        if (path && !path->empty())
        {
            maps.push_back(std::move(*path));
        }
    }
    return maps;
}

std::expected<void, std::string> SaveRecentMaps(const std::filesystem::path& file, std::span<const std::filesystem::path> maps)
{
    std::string text(kRecentHeader);
    text += '\n';
    for (const auto& map : maps)
    {
        text += escape(utf8(map));
        text += '\n';
    }
    return writeAtomically(file, text);
}

void RememberRecentMap(std::vector<std::filesystem::path>& recent, const std::filesystem::path& file)
{
    std::erase_if(recent, [&file](const std::filesystem::path& known)
                  {
                      std::error_code error;
                      return known.lexically_normal() == file.lexically_normal() || (std::filesystem::equivalent(known, file, error) && !error);
                  });
    recent.insert(recent.begin(), file);
    if (recent.size() > kRecentMapLimit)
    {
        recent.resize(kRecentMapLimit);
    }
}

std::filesystem::path MapFileName(std::string_view identity)
{
    std::string name;
    for (const char character : identity)
    {
        const auto value = static_cast<unsigned char>(character);
        name += value < 0x20 || std::string_view("<>:\"/\\|?*").find(character) != std::string_view::npos ? '_' : character;
    }
    // Windows drops trailing dots and spaces from file names.
    while (!name.empty() && (name.back() == '.' || name.back() == ' '))
    {
        name.pop_back();
    }
    // Device names stay reserved on Windows even with an extension.
    std::string upper;
    std::ranges::transform(name, std::back_inserter(upper), [](char character)
                           {
                               return character >= 'a' && character <= 'z' ? static_cast<char>(character - 'a' + 'A') : character;
                           });
    const bool numbered = upper.size() == 4 && (upper.starts_with("COM") || upper.starts_with("LPT")) && upper[3] >= '1' && upper[3] <= '9';
    if (numbered || upper == "CON" || upper == "PRN" || upper == "AUX" || upper == "NUL")
    {
        name += '_';
    }
    auto path = fromUtf8(name.empty() ? std::string("Untitled") : name).value_or(std::filesystem::path("Untitled"));
    path += std::string(kMapDraftExtension);
    return path;
}

std::int64_t MapDraftClockNow()
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string FormatMapDraftAge(std::int64_t savedAt, std::int64_t now)
{
    const auto age    = std::max<std::int64_t>(0, now - savedAt);
    const auto plural = [](std::int64_t count, std::string_view unit)
    {
        return std::to_string(count) + ' ' + std::string(unit) + (count == 1 ? "" : "s") + " ago";
    };
    if (age < 60)
    {
        return "just now";
    }
    if (age < 3600)
    {
        return plural(age / 60, "minute");
    }
    if (age < 86400)
    {
        return plural(age / 3600, "hour");
    }
    if (age < 2 * 86400)
    {
        return "yesterday";
    }
    return plural(age / 86400, "day");
}

} // namespace rerevved::tools
