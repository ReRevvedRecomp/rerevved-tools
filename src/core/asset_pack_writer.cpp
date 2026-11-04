#include "asset_pack_writer.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <regex>
#include <set>
#include <system_error>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#elif defined(__linux__)
#include <cerrno>
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace rerevved::tools
{
namespace
{

using Violations = std::vector<AssetPackViolation>;

void add(Violations& violations, AssetPackError error, const std::filesystem::path& path = {}, std::string field = {}, bool warning = false)
{
    violations.push_back({ error, path, std::move(field), warning });
}

bool validId(std::string_view id)
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

bool validVersion(std::string_view version)
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

std::optional<std::vector<std::uint32_t>> codePoints(std::string_view text)
{
    std::vector<std::uint32_t> result;
    for (std::size_t index = 0; index < text.size();)
    {
        const auto    lead      = static_cast<unsigned char>(text[index++]);
        std::uint32_t value     = lead;
        unsigned int  remaining = 0;
        std::uint32_t minimum   = 0;
        if (lead >= 0xC2 && lead <= 0xDF)
        {
            value     = lead & 0x1FU;
            remaining = 1;
            minimum   = 0x80;
        }
        else if (lead >= 0xE0 && lead <= 0xEF)
        {
            value     = lead & 0x0FU;
            remaining = 2;
            minimum   = 0x800;
        }
        else if (lead >= 0xF0 && lead <= 0xF4)
        {
            value     = lead & 0x07U;
            remaining = 3;
            minimum   = 0x10000;
        }
        else if (lead >= 0x80)
        {
            return std::nullopt;
        }
        for (unsigned int count = 0; count < remaining; ++count)
        {
            if (index == text.size())
            {
                return std::nullopt;
            }
            const auto next = static_cast<unsigned char>(text[index++]);
            if ((next & 0xC0U) != 0x80U)
            {
                return std::nullopt;
            }
            value = (value << 6U) | (next & 0x3FU);
        }
        if (value < minimum || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF))
        {
            return std::nullopt;
        }
        result.push_back(value);
    }
    return result;
}

bool pythonWhitespace(std::uint32_t value)
{
    return (value >= 0x09 && value <= 0x0D) || (value >= 0x1C && value <= 0x20) ||
           value == 0x85 || value == 0xA0 || value == 0x1680 ||
           (value >= 0x2000 && value <= 0x200A) || value == 0x2028 || value == 0x2029 ||
           value == 0x202F || value == 0x205F || value == 0x3000;
}

bool nonemptyName(std::string_view text)
{
    const auto points = codePoints(text);
    return points && std::ranges::any_of(*points, [](auto value)
                                         {
                                             return !pythonWhitespace(value);
                                         });
}

bool validKey(std::string_view key)
{
    if (key.empty() || key.front() == '/' || key.find('\\') != std::string_view::npos ||
        (key.size() >= 2 && key[1] == ':') || key.find('\0') != std::string_view::npos || !codePoints(key))
    {
        return false;
    }
#ifdef _WIN32
    // Colons can select an NTFS stream instead of creating the declared regular asset file.
    if (key.find(':') != std::string_view::npos)
    {
        return false;
    }
#endif
    for (std::size_t start = 0; start <= key.size();)
    {
        const auto end  = key.find('/', start);
        const auto part = key.substr(start, end == std::string_view::npos ? end : end - start);
        if (part.empty() || part == "." || part == "..")
        {
            return false;
        }
        if (end == std::string_view::npos)
        {
            break;
        }
        start = end + 1;
    }
    return true;
}

bool validWriterKey(std::string_view key)
{
    if (!validKey(key) || key.find_first_of("<>\":|?*") != std::string_view::npos ||
        std::ranges::any_of(key, [](char character)
                            {
                                const auto value = static_cast<unsigned char>(character);
                                return value < 0x20 || value == 0x7F;
                            }))
    {
        return false;
    }
    // Packs target Windows even when authored elsewhere. Reject aliases before creating files.
    for (std::size_t start = 0; start < key.size();)
    {
        const auto end  = key.find('/', start);
        const auto part = key.substr(start, end == std::string_view::npos ? end : end - start);
        if (part.back() == '.' || part.back() == ' ')
        {
            return false;
        }
        std::string base(part.substr(0, part.find('.')));
        // Windows ignores trailing spaces before an extension, so "CON .txt" is the device.
        while (!base.empty() && base.back() == ' ')
        {
            base.pop_back();
        }
        for (auto& character : base)
        {
            if (character >= 'a' && character <= 'z')
            {
                character = static_cast<char>(character - 'a' + 'A');
            }
        }
        if (base == "CON" || base == "PRN" || base == "AUX" || base == "NUL" || base == "CONIN$" || base == "CONOUT$" ||
            (base.size() == 4 && (base.starts_with("COM") || base.starts_with("LPT")) && base[3] >= '0' && base[3] <= '9'))
        {
            return false;
        }
        if (end == std::string_view::npos)
        {
            break;
        }
        start = end + 1;
    }
    return true;
}

std::filesystem::path utf8Path(std::string_view text)
{
    std::u8string result;
    for (const auto character : text)
    {
        result.push_back(static_cast<char8_t>(character));
    }
    return std::filesystem::path(result);
}

bool reparsePoint(const std::filesystem::path& path)
{
#ifdef _WIN32
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    std::error_code error;
    return std::filesystem::is_symlink(std::filesystem::symlink_status(path, error));
#endif
}

bool safeAncestors(const std::filesystem::path& path)
{
    std::error_code error;
    const auto      absolute = std::filesystem::absolute(path, error);
    if (error)
    {
        return false;
    }
    auto current = absolute.root_path();
    for (const auto& part : absolute.relative_path())
    {
        current /= part;
        if (reparsePoint(current))
        {
            return false;
        }
    }
    return true;
}

Violations validateDescription(const AssetPackDescription& description)
{
    Violations violations;
    if (description.manifestVersion != 1)
    {
        add(violations, AssetPackError::InvalidManifestVersion, {}, "manifest_version");
    }
    if (!validId(description.id))
    {
        add(violations, AssetPackError::InvalidId, {}, "id");
    }
    if (!nonemptyName(description.name))
    {
        add(violations, AssetPackError::InvalidName, {}, "name");
    }
    if (!validVersion(description.version))
    {
        add(violations, AssetPackError::InvalidVersion, {}, "version");
    }
    for (const auto* value : { &description.name, description.author ? &*description.author : nullptr, description.description ? &*description.description : nullptr })
    {
        if (value && !codePoints(*value))
        {
            add(violations, AssetPackError::InvalidEncoding);
        }
    }
    if (description.mapPack)
    {
        for (const auto& scenario : description.mapPack->scenarios)
        {
            if (!codePoints(scenario.name))
            {
                add(violations, AssetPackError::InvalidEncoding, {}, "map_pack.scenarios.name");
            }
        }
    }
    if (description.assets.empty())
    {
        add(violations, AssetPackError::EmptyAssets);
    }
    std::set<std::string> keys;
    for (const auto& asset : description.assets)
    {
        if (!validWriterKey(asset.keyPath))
        {
            add(violations, AssetPackError::InvalidKeyPath, {}, asset.keyPath);
        }
        if (!keys.insert(asset.keyPath).second)
        {
            add(violations, AssetPackError::DuplicateKeyPath, {}, asset.keyPath);
        }
    }
    for (const auto& key : keys)
    {
        for (auto slash = key.find('/'); slash != std::string::npos; slash = key.find('/', slash + 1))
        {
            if (keys.contains(key.substr(0, slash)))
            {
                add(violations, AssetPackError::InvalidKeyPath, {}, key);
            }
        }
    }
    return violations;
}

void appendUtf8(std::string& result, std::uint32_t value)
{
    if (value <= 0x7F)
    {
        result.push_back(static_cast<char>(value));
    }
    else if (value <= 0x7FF)
    {
        result.push_back(static_cast<char>(0xC0U | (value >> 6U)));
        result.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
    }
    else if (value <= 0xFFFF)
    {
        result.push_back(static_cast<char>(0xE0U | (value >> 12U)));
        result.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3FU)));
        result.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
    }
    else
    {
        result.push_back(static_cast<char>(0xF0U | (value >> 18U)));
        result.push_back(static_cast<char>(0x80U | ((value >> 12U) & 0x3FU)));
        result.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3FU)));
        result.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
    }
}

std::string quote(std::string_view value)
{
    constexpr std::string_view digits = "0123456789ABCDEF";
    std::string                result = "\"";
    const auto                 points = codePoints(value);
    for (const auto point : *points)
    {
        switch (point)
        {
            case '"':
                result += "\\\"";
                break;
            case '\\':
                result += "\\\\";
                break;
            case '\b':
                result += "\\b";
                break;
            case '\t':
                result += "\\t";
                break;
            case '\n':
                result += "\\n";
                break;
            case '\f':
                result += "\\f";
                break;
            case '\r':
                result += "\\r";
                break;
            default:
                if (point >= 0x20 && point < 0x7F)
                {
                    result.push_back(static_cast<char>(point));
                }
                else
                {
                    const auto count = point <= 0xFFFF ? 4 : 8;
                    result += count == 4 ? "\\u" : "\\U";
                    for (int index = count - 1; index >= 0; --index)
                    {
                        result.push_back(digits[(point >> (index * 4)) & 0xFU]);
                    }
                }
        }
    }
    result += '"';
    return result;
}

std::string manifestText(const AssetPackDescription& description)
{
    std::string text = "manifest_version = 1\n\n[asset_pack]\n";
    text += "id = " + quote(description.id) + "\n";
    text += "name = " + quote(description.name) + "\n";
    text += "version = " + quote(description.version) + "\n";
    if (description.author)
    {
        text += "author = " + quote(*description.author) + "\n";
    }
    if (description.description)
    {
        text += "description = " + quote(*description.description) + "\n";
    }
    if (description.mapPack)
    {
        text += "\n[map_pack]\nslot = " + std::to_string(description.mapPack->slot) + "\n";
        for (const auto& scenario : description.mapPack->scenarios)
        {
            text += "\n[[map_pack.scenarios]]\nid = " + std::to_string(scenario.id) + "\n";
            text += "name = " + quote(scenario.name) + "\n";
        }
    }
    return text;
}

std::string_view trim(std::string_view text)
{
    const auto start = text.find_first_not_of(" \t\r");
    if (start == std::string_view::npos)
    {
        return {};
    }
    return text.substr(start, text.find_last_not_of(" \t\r") - start + 1);
}

struct ManifestValue
{
    bool        string = false;
    std::string text;
};

bool integerOne(const ManifestValue& value)
{
    if (value.string)
    {
        return false;
    }
    std::string text;
    std::ranges::copy_if(value.text, std::back_inserter(text), [](char character)
                         {
                             return character != '_';
                         });
    if (text == "1" || text == "+1")
    {
        return true;
    }
    if (!text.starts_with("0x") && !text.starts_with("0o") && !text.starts_with("0b"))
    {
        return false;
    }
    const auto digits = std::string_view(text).substr(2);
    const auto first  = digits.find_first_not_of('0');
    return first != std::string_view::npos && digits.substr(first) == "1";
}

bool decimalInteger(const ManifestValue& value)
{
    // TOML integers are signed 64 bit. A wider literal fails a conforming reader.
    static const std::regex integer(R"([+-]?(0|[1-9][0-9]*))");
    if (value.string || !std::regex_match(value.text, integer))
    {
        return false;
    }
    const auto*  first = value.text.data() + (value.text.front() == '+' ? 1 : 0);
    std::int64_t parsed{};
    const auto [end, error] = std::from_chars(first, value.text.data() + value.text.size(), parsed);
    return error == std::errc{} && end == value.text.data() + value.text.size();
}

// This reader deliberately reports unsupported TOML constructs instead of guessing their meaning.
std::optional<ManifestValue> readValue(std::string_view text)
{
    text = trim(text);
    if (text.empty())
    {
        return std::nullopt;
    }
    if (text.front() != '"' && text.front() != '\'')
    {
        const std::string       scalar(trim(text.substr(0, text.find('#'))));
        static const std::regex number(
            R"([+-]?(0|[1-9](_?[0-9])*)(\.[0-9](_?[0-9])*)?([eE][+-]?[0-9](_?[0-9])*)?|0x[0-9a-fA-F](_?[0-9a-fA-F])*|0o[0-7](_?[0-7])*|0b[01](_?[01])*|[+-]?(inf|nan))");
        if (scalar != "true" && scalar != "false" && !std::regex_match(scalar, number))
        {
            return std::nullopt;
        }
        return ManifestValue{ false, scalar };
    }
    const auto  delimiter = text.front();
    std::string result;
    for (std::size_t index = 1; index < text.size(); ++index)
    {
        auto character = text[index];
        if (character == delimiter)
        {
            const auto rest = trim(text.substr(index + 1));
            if (!rest.empty() && rest.front() != '#')
            {
                return std::nullopt;
            }
            return ManifestValue{ true, std::move(result) };
        }
        if (static_cast<unsigned char>(character) < 0x20 && character != '\t')
        {
            return std::nullopt;
        }
        if (character == 0x7F)
        {
            return std::nullopt;
        }
        if (delimiter == '"' && character == '\\')
        {
            if (++index == text.size())
            {
                return std::nullopt;
            }
            character = text[index];
            switch (character)
            {
                case 'b':
                    result += '\b';
                    break;
                case 't':
                    result += '\t';
                    break;
                case 'n':
                    result += '\n';
                    break;
                case 'f':
                    result += '\f';
                    break;
                case 'r':
                    result += '\r';
                    break;
                case '"':
                    result += '"';
                    break;
                case '\\':
                    result += '\\';
                    break;
                case 'u':
                case 'U':
                {
                    const std::size_t count = character == 'u' ? 4 : 8;
                    if (text.size() - index - 1 < count)
                    {
                        return std::nullopt;
                    }
                    std::uint32_t point = 0;
                    for (std::size_t digit = 0; digit < count; ++digit)
                    {
                        const auto hex   = text[++index];
                        const auto value = hex >= '0' && hex <= '9' ? hex - '0' : hex >= 'a' && hex <= 'f' ? hex - 'a' + 10
                                                                              : hex >= 'A' && hex <= 'F'   ? hex - 'A' + 10
                                                                                                           : -1;
                        if (value < 0)
                        {
                            return std::nullopt;
                        }
                        point = (point << 4U) | static_cast<std::uint32_t>(value);
                    }
                    if (point > 0x10FFFF || (point >= 0xD800 && point <= 0xDFFF))
                    {
                        return std::nullopt;
                    }
                    appendUtf8(result, point);
                    break;
                }
                default:
                    return std::nullopt;
            }
        }
        else
        {
            result += character;
        }
    }
    return std::nullopt;
}

using ManifestFields = std::map<std::string, ManifestValue>;

ManifestFields validateManifest(std::string_view text, const std::filesystem::path& path, std::string_view expectedId, Violations& violations)
{
    if (std::ranges::any_of(text, [](unsigned char value)
                            {
                                return value > 0x7F;
                            }))
    {
        add(violations, AssetPackError::InvalidEncoding, path);
        return {};
    }
    for (std::size_t index = 0; index < text.size(); ++index)
    {
        const auto value = static_cast<unsigned char>(text[index]);
        if ((value < 0x20 && value != '\t' && value != '\n' && value != '\r') || value == 0x7F ||
            (value == '\r' && (index + 1 == text.size() || text[index + 1] != '\n')))
        {
            add(violations, AssetPackError::InvalidManifest, path);
            return {};
        }
    }
    ManifestFields        fields;
    std::set<std::string> defined;
    bool                  packTable = false;
    std::string           table;
    std::size_t           scenarioTables = 0;
    for (std::size_t start = 0; start < text.size();)
    {
        const auto end  = text.find('\n', start);
        const auto line = trim(text.substr(start, end == std::string_view::npos ? end : end - start));
        start           = end == std::string_view::npos ? text.size() : end + 1;
        if (line.empty() || line.front() == '#')
        {
            continue;
        }
        if (line.starts_with("[["))
        {
            const auto close = line.find("]]");
            const auto rest  = close == std::string_view::npos ? line : trim(line.substr(close + 2));
            if (close == std::string_view::npos || trim(line.substr(2, close - 2)) != "map_pack.scenarios" || (!rest.empty() && rest.front() != '#'))
            {
                add(violations, AssetPackError::UnsupportedManifestSyntax, path);
                continue;
            }
            table = "map_pack.scenarios." + std::to_string(scenarioTables++);
            continue;
        }
        if (line.front() == '[')
        {
            const auto close = line.find(']');
            const auto rest  = close == std::string_view::npos ? line : trim(line.substr(close + 1));
            if (close == std::string_view::npos || (!rest.empty() && rest.front() != '#'))
            {
                add(violations, AssetPackError::UnsupportedManifestSyntax, path);
                continue;
            }
            table = std::string(trim(line.substr(1, close - 1)));
            if (table.empty() || table.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != std::string::npos)
            {
                add(violations, AssetPackError::UnsupportedManifestSyntax, path, table);
                continue;
            }
            if (!defined.insert("[" + table + "]").second)
            {
                add(violations, AssetPackError::InvalidManifest, path, table);
            }
            if (table == "asset_pack")
            {
                packTable = true;
            }
            else if (table != "map_pack")
            {
                add(violations, AssetPackError::UnknownField, path, table, true);
            }
            continue;
        }
        const auto equal = line.find('=');
        if (equal == std::string_view::npos)
        {
            add(violations, AssetPackError::UnsupportedManifestSyntax, path);
            continue;
        }
        const auto key = trim(line.substr(0, equal));
        if (key.empty() || key.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != std::string_view::npos)
        {
            add(violations, AssetPackError::UnsupportedManifestSyntax, path);
            continue;
        }
        const std::string fullKey = table.empty() ? std::string(key) : table + "." + std::string(key);
        if (!defined.insert(fullKey).second)
        {
            add(violations, AssetPackError::InvalidManifest, path, fullKey);
        }
        const auto value = readValue(line.substr(equal + 1));
        if (!value)
        {
            add(violations, AssetPackError::UnsupportedManifestSyntax, path, fullKey);
            continue;
        }
        const bool scenario = table.starts_with("map_pack.scenarios.");
        const bool known    = (table.empty() && key == "manifest_version") ||
                              (table == "asset_pack" && (key == "id" || key == "name" || key == "version" || key == "author" || key == "description")) ||
                              (table == "map_pack" && key == "slot") || (scenario && (key == "id" || key == "name"));
        if (!known)
        {
            add(violations, AssetPackError::UnknownField, path, fullKey, true);
        }
        else if ((table == "map_pack" || (scenario && key == "id")) ? !decimalInteger(*value) : (scenario && !value->string))
        {
            add(violations, AssetPackError::InvalidManifest, path, fullKey);
        }
        if (fullKey == "asset_pack")
        {
            add(violations, AssetPackError::MissingPackTable, path);
        }
        fields[fullKey] = *value;
    }
    const auto manifestVersion = fields.find("manifest_version");
    if (manifestVersion == fields.end() || !integerOne(manifestVersion->second))
    {
        add(violations, AssetPackError::InvalidManifestVersion, path, "manifest_version");
    }
    if (!packTable)
    {
        add(violations, AssetPackError::MissingPackTable, path);
    }
    for (const auto key : { "id", "name", "version", "author", "description" })
    {
        const auto found = fields.find(std::string("asset_pack.") + key);
        if (found == fields.end())
        {
            if (std::string_view(key) == "id" || std::string_view(key) == "name" || std::string_view(key) == "version")
            {
                add(violations, AssetPackError::MissingField, path, key);
            }
            continue;
        }
        const auto&    value = found->second;
        AssetPackError error = AssetPackError::InvalidDescription;
        bool           valid = value.string;
        if (std::string_view(key) == "id")
        {
            error = AssetPackError::InvalidId;
            valid = valid && validId(value.text);
            if (valid && value.text != expectedId)
            {
                add(violations, AssetPackError::IdMismatch, path, key);
            }
        }
        else if (std::string_view(key) == "name")
        {
            error = AssetPackError::InvalidName;
            valid = valid && nonemptyName(value.text);
        }
        else if (std::string_view(key) == "version")
        {
            error = AssetPackError::InvalidVersion;
            valid = valid && validVersion(value.text);
        }
        else if (std::string_view(key) == "author")
        {
            error = AssetPackError::InvalidAuthor;
        }
        if (!valid)
        {
            add(violations, error, path, key);
        }
    }
    return fields;
}

std::expected<std::string, AssetPackError> readFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        return std::unexpected(AssetPackError::OpenFailed);
    }
    std::string result{ std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
    if (input.bad())
    {
        return std::unexpected(AssetPackError::ReadFailed);
    }
    return result;
}

std::expected<void, AssetPackError> writeFile(const std::filesystem::path& path, const char* bytes, std::size_t size)
{
    if (size > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
    {
        return std::unexpected(AssetPackError::WriteFailed);
    }
    std::ofstream output(path, std::ios::binary | std::ios::noreplace);
    if (!output)
    {
        return std::unexpected(AssetPackError::OpenFailed);
    }
    if (size != 0)
    {
        output.write(bytes, static_cast<std::streamsize>(size));
    }
    if (!output)
    {
        return std::unexpected(AssetPackError::WriteFailed);
    }
    output.close();
    if (output.fail())
    {
        return std::unexpected(AssetPackError::FinalizationFailed);
    }
    return {};
}

bool removeStaging(const std::filesystem::path& path)
{
    std::error_code error;
    if (!reparsePoint(path) && std::filesystem::is_directory(std::filesystem::symlink_status(path, error)))
    {
        std::filesystem::directory_iterator       iterator(path, error);
        const std::filesystem::directory_iterator end;
        while (!error && iterator != end)
        {
            if (!removeStaging(iterator->path()))
            {
                return false;
            }
            iterator.increment(error);
        }
        if (error)
        {
            return false;
        }
    }
    std::filesystem::remove(path, error);
    return !error;
}

std::expected<void, AssetPackError> publish(const std::filesystem::path& source, const std::filesystem::path& destination)
{
#ifdef _WIN32
    if (MoveFileExW(source.c_str(), destination.c_str(), 0) != 0)
    {
        return {};
    }
    const auto      nativeError = GetLastError();
    std::error_code statusError;
    const auto      status = std::filesystem::symlink_status(destination, statusError);
    return std::unexpected(nativeError == ERROR_ALREADY_EXISTS || nativeError == ERROR_FILE_EXISTS ||
                                   (!statusError && status.type() != std::filesystem::file_type::not_found)
                               ? AssetPackError::DestinationExists
                               : AssetPackError::PublishFailed);
#elif defined(__linux__)
    // RENAME_NOREPLACE protects even an empty destination created after the preflight check.
    if (syscall(SYS_renameat2, AT_FDCWD, source.c_str(), AT_FDCWD, destination.c_str(), 1U) == 0)
    {
        return {};
    }
    return std::unexpected(errno == EEXIST ? AssetPackError::DestinationExists : AssetPackError::PublishFailed);
#else
    static_cast<void>(source);
    static_cast<void>(destination);
    return std::unexpected(AssetPackError::PublishFailed);
#endif
}

} // namespace

std::vector<AssetPackViolation> ValidateAssetPack(const std::filesystem::path& packDirectory)
{
    Violations      violations;
    std::error_code error;
    if (!safeAncestors(packDirectory))
    {
        add(violations, AssetPackError::ReparsePoint, packDirectory);
        return violations;
    }
    if (!std::filesystem::is_directory(packDirectory, error) || error)
    {
        add(violations, AssetPackError::InvalidPackDirectory, packDirectory);
        return violations;
    }
    const auto        normalized = packDirectory.lexically_normal();
    const auto        encodedId  = (normalized.has_filename() ? normalized.filename() : normalized.parent_path().filename()).generic_u8string();
    const std::string id(encodedId.begin(), encodedId.end());
    if (!validId(id))
    {
        add(violations, AssetPackError::InvalidId, packDirectory);
    }
    const auto manifest = packDirectory / "asset-pack.toml";
    if (reparsePoint(manifest))
    {
        add(violations, AssetPackError::ReparsePoint, manifest);
    }
    else if (!std::filesystem::is_regular_file(manifest, error))
    {
        add(violations, AssetPackError::MissingManifest, manifest);
    }
    else
    {
        const auto text = readFile(manifest);
        if (!text)
        {
            add(violations, text.error(), manifest);
        }
        else
        {
            static_cast<void>(validateManifest(*text, manifest, id, violations));
        }
    }
    error.clear();
    const auto assets = packDirectory / "assets";
    if (reparsePoint(assets))
    {
        add(violations, AssetPackError::ReparsePoint, assets);
    }
    else if (!std::filesystem::is_directory(assets, error))
    {
        add(violations, AssetPackError::InvalidAssetsDirectory, assets);
    }
    bool                               hasAssets = false;
    std::vector<std::filesystem::path> pending{ packDirectory };
    while (!pending.empty())
    {
        const auto directory = pending.back();
        pending.pop_back();
        error.clear();
        std::filesystem::directory_iterator       iterator(directory, error);
        const std::filesystem::directory_iterator end;
        while (!error && iterator != end)
        {
            const auto path     = iterator->path();
            const auto relative = path.lexically_relative(packDirectory);
            if (reparsePoint(path))
            {
                add(violations, AssetPackError::ReparsePoint, path);
            }
            else
            {
                std::error_code statusError;
                const auto      status = iterator->symlink_status(statusError);
                if (statusError)
                {
                    add(violations, AssetPackError::ReadFailed, path);
                }
                else if (std::filesystem::is_directory(status))
                {
                    pending.push_back(path);
                }
                else if (!std::filesystem::is_regular_file(status))
                {
                    add(violations, AssetPackError::NonRegularFile, path);
                }
                else
                {
                    const auto        generic = relative.generic_u8string();
                    const std::string name(generic.begin(), generic.end());
                    if (name.starts_with("assets/"))
                    {
                        hasAssets = true;
                        if (!validKey(std::string_view(name).substr(7)))
                        {
                            add(violations, AssetPackError::InvalidKeyPath, path);
                        }
                    }
                    else if (name != "asset-pack.toml" && name != "icon.png" && name != "README.md" &&
                             !(name.starts_with("LICENSE") && name.find('/') == std::string::npos))
                    {
                        add(violations, AssetPackError::UnsupportedFile, path);
                    }
                }
            }
            iterator.increment(error);
        }
        if (error)
        {
            add(violations, AssetPackError::ReadFailed, directory);
        }
    }
    if (!hasAssets)
    {
        add(violations, AssetPackError::EmptyAssets, assets);
    }
    std::ranges::sort(violations, [](const auto& left, const auto& right)
                      {
                          if (left.path != right.path)
                          {
                              return left.path < right.path;
                          }
                          if (left.field != right.field)
                          {
                              return left.field < right.field;
                          }
                          return left.error < right.error;
                      });
    return violations;
}

std::expected<AssetPackDescription, AssetPackError> ReadAssetPackManifest(const std::filesystem::path& packDirectory)
{
    for (const auto& violation : ValidateAssetPack(packDirectory))
    {
        if (!violation.warning)
        {
            return std::unexpected(violation.error);
        }
    }
    const auto text = readFile(packDirectory / "asset-pack.toml");
    if (!text)
    {
        return std::unexpected(text.error());
    }
    // The manifest is validated again as read, in case it changed since the pack was checked.
    Violations violations;
    const auto normalized = packDirectory.lexically_normal();
    const auto encodedId  = (normalized.has_filename() ? normalized.filename() : normalized.parent_path().filename()).generic_u8string();
    const auto fields     = validateManifest(*text, packDirectory / "asset-pack.toml", std::string(encodedId.begin(), encodedId.end()), violations);
    for (const auto& violation : violations)
    {
        if (!violation.warning)
        {
            return std::unexpected(violation.error);
        }
    }
    const auto find = [&](const std::string& key) -> const ManifestValue*
    {
        const auto found = fields.find(key);
        return found == fields.end() ? nullptr : &found->second;
    };
    const auto integer = [](const ManifestValue& value, auto& out)
    {
        const auto* first = value.text.data() + (value.text.starts_with('+') ? 1 : 0);
        return std::from_chars(first, value.text.data() + value.text.size(), out).ec == std::errc{};
    };
    AssetPackDescription description;
    description.id      = find("asset_pack.id")->text;
    description.name    = find("asset_pack.name")->text;
    description.version = find("asset_pack.version")->text;
    if (const auto* author = find("asset_pack.author"))
    {
        description.author = author->text;
    }
    if (const auto* about = find("asset_pack.description"))
    {
        description.description = about->text;
    }
    if (const auto* slot = find("map_pack.slot"))
    {
        MapPackInfo info;
        if (!integer(*slot, info.slot))
        {
            return std::unexpected(AssetPackError::InvalidManifest);
        }
        // Scenario tables are display metadata, so one without an ID is skipped.
        for (std::size_t index = 0; index < fields.size(); ++index)
        {
            const auto          table = "map_pack.scenarios." + std::to_string(index) + ".";
            const auto*         id    = find(table + "id");
            const auto*         name  = find(table + "name");
            MapPackScenarioInfo scenario;
            if (id != nullptr && integer(*id, scenario.id))
            {
                scenario.name = name != nullptr ? name->text : std::string();
                info.scenarios.push_back(std::move(scenario));
            }
        }
        description.mapPack = std::move(info);
    }
    return description;
}

std::expected<void, AssetPackError> WriteAssetPack(const AssetPackDescription&  description,
                                                   const std::filesystem::path& destinationRoot)
{
    const auto violations = validateDescription(description);
    if (!violations.empty())
    {
        return std::unexpected(violations.front().error);
    }
    std::error_code error;
    const auto      root = std::filesystem::absolute(destinationRoot, error);
    if (error)
    {
        return std::unexpected(AssetPackError::InvalidPackDirectory);
    }
    if (!safeAncestors(root))
    {
        return std::unexpected(AssetPackError::ReparsePoint);
    }
    if (!std::filesystem::is_directory(root, error) || error)
    {
        return std::unexpected(AssetPackError::InvalidPackDirectory);
    }
    const auto destination = root / description.id;
    auto       status      = std::filesystem::symlink_status(destination, error);
    if (status.type() != std::filesystem::file_type::not_found && !error)
    {
        return std::unexpected(AssetPackError::DestinationExists);
    }
    if (error && error != std::errc::no_such_file_or_directory)
    {
        return std::unexpected(AssetPackError::OpenFailed);
    }
    static std::atomic<std::uint64_t> sequence{ 0 };
    std::filesystem::path             staging;
    for (;;)
    {
        const auto token = std::chrono::steady_clock::now().time_since_epoch().count();
        staging          = root / (".asset-pack-stage-" + std::to_string(token) + "-" + std::to_string(sequence.fetch_add(1)));
        error.clear();
        if (std::filesystem::create_directory(staging, error))
        {
            break;
        }
        if (error && error != std::errc::file_exists)
        {
            return std::unexpected(AssetPackError::CreateDirectoryFailed);
        }
    }
    // Keeping the real ID below the staging holder lets the public validator check directory identity.
    const auto stagedPack = staging / description.id;
    const auto fail       = [&](AssetPackError failure) -> std::expected<void, AssetPackError>
    {
        return std::unexpected(removeStaging(staging) ? failure : AssetPackError::CleanupFailed);
    };
    if (!std::filesystem::create_directories(stagedPack / "assets", error) || error)
    {
        return fail(AssetPackError::CreateDirectoryFailed);
    }
    std::vector<const AssetPackAsset*> ordered;
    for (const auto& asset : description.assets)
    {
        ordered.push_back(&asset);
    }
    std::ranges::sort(ordered, {}, [](const auto* asset)
                      {
                          return asset->keyPath;
                      });
    for (const auto* asset : ordered)
    {
        const auto path = stagedPack / "assets" / utf8Path(asset->keyPath);
        std::filesystem::create_directories(path.parent_path(), error);
        if (error)
        {
            return fail(AssetPackError::CreateDirectoryFailed);
        }
        const auto written = writeFile(path, reinterpret_cast<const char*>(asset->bytes.data()), asset->bytes.size());
        if (!written)
        {
            return fail(written.error());
        }
    }
    const auto text    = manifestText(description);
    const auto written = writeFile(stagedPack / "asset-pack.toml", text.data(), text.size());
    if (!written)
    {
        return fail(written.error());
    }
    const auto stagedViolations = ValidateAssetPack(stagedPack);
    for (const auto& violation : stagedViolations)
    {
        if (!violation.warning)
        {
            return fail(violation.error);
        }
    }
    for (const auto& asset : description.assets)
    {
        const auto path = stagedPack / "assets" / utf8Path(asset.keyPath);
        if (reparsePoint(path))
        {
            return fail(AssetPackError::ReparsePoint);
        }
        if (!std::filesystem::is_regular_file(path, error) || error)
        {
            return fail(AssetPackError::NonRegularFile);
        }
        const auto bytes = readFile(path);
        if (!bytes)
        {
            return fail(bytes.error());
        }
        if (bytes->size() != asset.bytes.size() ||
            !std::equal(bytes->begin(), bytes->end(), asset.bytes.begin(), [](char left, std::byte right)
                        {
                            return static_cast<unsigned char>(left) == std::to_integer<unsigned char>(right);
                        }))
        {
            return fail(AssetPackError::ReadBackMismatch);
        }
    }
    const auto published = publish(stagedPack, destination);
    if (!published)
    {
        return fail(published.error());
    }
    // The holder is empty after publication. Never remove the published pack on a cleanup failure.
    std::filesystem::remove(staging, error);
    if (error)
    {
        return std::unexpected(AssetPackError::CleanupFailed);
    }
    return {};
}

std::string_view AssetPackErrorMessage(AssetPackError error)
{
    switch (error)
    {
        case AssetPackError::InvalidManifestVersion:
            return "manifest_version must be integer 1.";
        case AssetPackError::MissingPackTable:
            return "The [asset_pack] table is required.";
        case AssetPackError::MissingField:
            return "A required asset pack manifest field is missing.";
        case AssetPackError::InvalidId:
            return "The asset pack ID must use lowercase ASCII letters or digits "
                   "separated by hyphens, at most 63 characters.";
        case AssetPackError::IdMismatch:
            return "The asset pack ID does not match its directory name.";
        case AssetPackError::InvalidName:
            return "The asset pack name must be a nonempty string.";
        case AssetPackError::InvalidVersion:
            return "The asset pack version must use numeric major.minor syntax.";
        case AssetPackError::InvalidAuthor:
            return "The asset pack author must be a string.";
        case AssetPackError::InvalidDescription:
            return "The asset pack description must be a string.";
        case AssetPackError::InvalidEncoding:
            return "Manifest input must be ASCII and metadata strings must be valid UTF-8.";
        case AssetPackError::InvalidManifest:
            return "The asset pack manifest has duplicate or invalid definitions.";
        case AssetPackError::UnsupportedManifestSyntax:
            return "The asset pack manifest uses unsupported TOML syntax.";
        case AssetPackError::UnknownField:
            return "The asset pack manifest contains an unknown field.";
        case AssetPackError::InvalidKeyPath:
            return "The asset key must be a safe relative path with forward slashes.";
        case AssetPackError::DuplicateKeyPath:
            return "The asset pack contains a duplicate key path.";
        case AssetPackError::EmptyAssets:
            return "The asset pack requires a nonempty assets/ tree.";
        case AssetPackError::InvalidPackDirectory:
            return "The asset pack directory must be an existing regular directory.";
        case AssetPackError::MissingManifest:
            return "The asset pack requires a regular asset-pack.toml file.";
        case AssetPackError::InvalidAssetsDirectory:
            return "The asset pack requires a regular assets/ directory.";
        case AssetPackError::ReparsePoint:
            return "Asset pack paths must not traverse links or reparse points.";
        case AssetPackError::NonRegularFile:
            return "An asset pack entry is not a regular file.";
        case AssetPackError::UnsupportedFile:
            return "The asset pack contains an unsupported file outside assets/.";
        case AssetPackError::DestinationExists:
            return "The asset pack destination already exists.";
        case AssetPackError::CreateDirectoryFailed:
            return "Could not create an asset pack directory.";
        case AssetPackError::OpenFailed:
            return "Could not open an asset pack file.";
        case AssetPackError::ReadFailed:
            return "Could not read an asset pack file or directory.";
        case AssetPackError::WriteFailed:
            return "Could not write an asset pack file.";
        case AssetPackError::FinalizationFailed:
            return "Could not finalize an asset pack file.";
        case AssetPackError::ReadBackMismatch:
            return "Asset pack readback bytes differ from the supplied bytes.";
        case AssetPackError::PublishFailed:
            return "Could not publish the asset pack without replacing a destination.";
        case AssetPackError::CleanupFailed:
            return "Could not remove the asset pack staging directory owned by this task.";
    }
    return "Unknown asset pack error.";
}

} // namespace rerevved::tools
