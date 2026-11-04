#include "scenario_document.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <fstream>
#include <limits>
#include <set>
#include <system_error>
#include <utility>

namespace rerevved::tools
{
namespace
{

// Research RVA-F-0075: /variators/recognized and /variators/defaults.
constexpr std::array<ScenarioVariator, 35> kVariators{
    { { "BAGRESSIVE", 0, 1 }, { "BFECUNDITY", 1, 1 }, { "BARBVSGOODY", 2, 1 }, { "RESOURCEDENSITY", 3, 2 }, { "TECHNOLOGYRATE", 4, 1 }, { "UPGRADERATE", 5, 1 }, { "NAVALSUPPORT", 6, 1 }, { "MAXCITYSIZE", 7, 2 }, { "WONDERSEXPIRE", 8, 1 }, { "WARANDPEACE", 9, 1 }, { "MUSTWINBY", 10, 0 }, { "QUICKWIN", 11, 0 }, { "SPEEDMODE", 12, 0 }, { "UFOVISIT", 13, 0 }, { "STARTERA", 14, 0 }, { "STARTSIZE", 15, 0 }, { "CLIMATE", 16, 2 }, { "CANNOTWINBY", 17, 0 }, { "NOSPACERACE", 18, 0 }, { "STARTGOLD", 19, 0 }, { "CARAVANGOLD", 20, 1 }, { "ATTACKBONUS", 21, 0 }, { "GREATPEOPLERATE", 22, 1 }, { "QUARTERPRICELIB", 23, 0 }, { "MOREGOLDRESOURCE", 24, 0 }, { "MORETRADEPERERA", 25, 0 }, { "DISPLAYCARD", 30, -1 }, { "STARTYEAR", 31, 0 }, { "MAPNUMBER", 32, 0 }, { "STARTLOCME", 33, 0 }, { "STARTLOC0", 34, 0 }, { "STARTLOC1", 35, 0 }, { "STARTLOC2", 36, 0 }, { "STARTLOC3", 37, 0 }, { "STARTLOC4", 38, 0 } }
};
constexpr std::array<std::string_view, 5> kLocales{ "ENG", "FRA", "ITA", "DEU", "ESP" };

bool isLetter(char ch)
{
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
}

bool isWhitespace(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

bool isWhitespace(std::string_view text)
{
    return std::ranges::all_of(text, [](char ch)
                               {
                                   return isWhitespace(ch);
                               });
}

bool isLiteral(std::string_view text)
{
    // Avoid the unclosed entity and TinyXML whitespace condensation boundaries.
    if (!text.empty() && (text.front() == ' ' || text.back() == ' ' || text.find("  ") != std::string_view::npos))
    {
        return false;
    }
    return std::ranges::all_of(text, [](unsigned char ch)
                               {
                                   return ch >= 0x20 && ch <= 0x7E && ch != '&' && ch != '<' && ch != '>';
                               });
}

// Retail packs use names up to 20 characters, such as Mountains_of_Madness.
bool isMap(std::string_view map)
{
    if (map == "NONE")
    {
        return true;
    }
    const auto folded = [](char ch)
    {
        return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch + ('a' - 'A')) : ch;
    };
    return !map.empty() && map.size() <= kScenarioMaximumMapName && isLetter(map.front()) &&
           !std::ranges::equal(map, std::string_view("none"), {}, folded) &&
           std::ranges::all_of(map, [](char ch)
                               {
                                   return isLetter(ch) || (ch >= '0' && ch <= '9') || ch == '_';
                               });
}

bool isExcluded(std::string_view name)
{
    return name == "EntryType" || name == "EntryTag" || name == "MAPNUMBER" || name == "STARTLOCME" ||
           name == "STARTLOC0" || name == "STARTLOC1" || name == "STARTLOC2" || name == "STARTLOC3" || name == "STARTLOC4";
}

ScenarioIssue issue(ScenarioError code, std::string_view field = {}, std::size_t index = kScenarioNoEntry)
{
    return { code, index, std::string(field) };
}

struct Attribute
{
    std::string_view name;
    std::string_view value;
};

struct Element
{
    std::string_view       name;
    std::string_view       text;
    std::vector<Attribute> attributes;
    std::vector<Element>   children;
};

class XmlReader
{
public:
    explicit XmlReader(std::string_view text)
    : text_(text)
    {
    }

    std::expected<Element, ScenarioIssue> read()
    {
        skipWhitespace();
        auto root = element(1);
        if (!root)
        {
            return root;
        }
        skipWhitespace();
        if (position_ != text_.size())
        {
            return std::unexpected(issue(ScenarioError::MalformedXml));
        }
        return root;
    }

private:
    void skipWhitespace()
    {
        while (position_ < text_.size() && isWhitespace(text_[position_]))
        {
            ++position_;
        }
    }

    bool take(std::string_view value)
    {
        if (!text_.substr(position_).starts_with(value))
        {
            return false;
        }
        position_ += value.size();
        return true;
    }

    std::string_view name()
    {
        const auto start = position_;
        if (position_ == text_.size() || (!isLetter(text_[position_]) && text_[position_] != '_'))
        {
            return {};
        }
        ++position_;
        while (position_ < text_.size())
        {
            const char ch = text_[position_];
            if (!isLetter(ch) && (ch < '0' || ch > '9') && ch != '_' && ch != '-' && ch != '.')
            {
                break;
            }
            ++position_;
        }
        return text_.substr(start, position_ - start);
    }

    std::expected<Element, ScenarioIssue> element(unsigned depth)
    {
        // The deepest admitted shape is root/EntryInfo/TITLE/ENG (or VARIATOR/specs).
        if (depth > 4 || ++nodes_ > 65536)
        {
            return std::unexpected(issue(ScenarioError::ResourceLimit));
        }
        if (!take("<"))
        {
            return std::unexpected(issue(ScenarioError::MalformedXml));
        }
        if (position_ < text_.size() && (text_[position_] == '!' || text_[position_] == '?'))
        {
            return std::unexpected(issue(ScenarioError::UnsupportedXml));
        }
        Element result;
        result.name = name();
        if (result.name.empty())
        {
            return std::unexpected(issue(ScenarioError::MalformedXml));
        }
        for (;;)
        {
            const auto beforeWhitespace = position_;
            skipWhitespace();
            if (take("/>"))
            {
                return result;
            }
            if (take(">"))
            {
                break;
            }
            if (position_ == beforeWhitespace)
            {
                return std::unexpected(issue(ScenarioError::MalformedXml, result.name));
            }
            const auto attributeName = name();
            if (attributeName.empty())
            {
                return std::unexpected(issue(ScenarioError::MalformedXml, result.name));
            }
            if (result.attributes.size() == 2)
            {
                return std::unexpected(issue(ScenarioError::UnknownAttribute, attributeName));
            }
            if (std::ranges::any_of(result.attributes, [&](const Attribute& a)
                                    {
                                        return a.name == attributeName;
                                    }))
            {
                return std::unexpected(issue(ScenarioError::MalformedXml, attributeName));
            }
            skipWhitespace();
            if (!take("="))
            {
                return std::unexpected(issue(ScenarioError::MalformedXml, attributeName));
            }
            skipWhitespace();
            if (!take("\""))
            {
                return std::unexpected(issue(ScenarioError::UnsupportedXml, attributeName));
            }
            const auto start = position_;
            const auto end   = text_.find('"', start);
            if (end == std::string_view::npos)
            {
                return std::unexpected(issue(ScenarioError::MalformedXml, attributeName));
            }
            const auto value = text_.substr(start, end - start);
            if (!isLiteral(value))
            {
                return std::unexpected(issue(ScenarioError::UnsupportedXml, attributeName));
            }
            result.attributes.push_back({ attributeName, value });
            position_ = end + 1;
        }
        bool hasText = false;
        for (;;)
        {
            if (take("</"))
            {
                const auto closeName = name();
                skipWhitespace();
                if (closeName != result.name || !take(">"))
                {
                    return std::unexpected(issue(ScenarioError::MalformedXml, result.name));
                }
                return result;
            }
            if (position_ == text_.size())
            {
                return std::unexpected(issue(ScenarioError::MalformedXml, result.name));
            }
            if (text_[position_] == '<')
            {
                if (hasText && !isWhitespace(result.text))
                {
                    return std::unexpected(issue(ScenarioError::MalformedXml, result.name));
                }
                auto child = element(depth + 1);
                if (!child)
                {
                    return std::unexpected(child.error());
                }
                result.children.push_back(std::move(*child));
            }
            else
            {
                const auto start = position_;
                const auto end   = text_.find('<', start);
                if (end == std::string_view::npos)
                {
                    return std::unexpected(issue(ScenarioError::MalformedXml, result.name));
                }
                const auto text = text_.substr(start, end - start);
                if ((!result.children.empty() || hasText) && !isWhitespace(text))
                {
                    return std::unexpected(issue(ScenarioError::MalformedXml, result.name));
                }
                if (!hasText)
                {
                    result.text = text;
                    hasText     = true;
                }
                position_ = end;
            }
        }
    }

    std::string_view text_;
    std::size_t      position_ = 0;
    std::size_t      nodes_    = 0;
};

std::optional<ScenarioIssue> checkContainer(const Element& node)
{
    if (!node.attributes.empty())
    {
        return issue(ScenarioError::UnknownAttribute, node.attributes.front().name);
    }
    if (!isWhitespace(node.text))
    {
        return issue(ScenarioError::MalformedXml, node.name);
    }
    return {};
}

std::expected<std::string_view, ScenarioIssue> scalar(const Element& node)
{
    if (!node.attributes.empty())
    {
        return std::unexpected(issue(ScenarioError::UnknownAttribute, node.attributes.front().name));
    }
    if (!node.children.empty())
    {
        return std::unexpected(issue(ScenarioError::UnknownField, node.children.front().name));
    }
    if (node.text.empty())
    {
        return std::unexpected(issue(ScenarioError::EmptyScalar, node.name));
    }
    if (node.text.size() > kScenarioMaximumLiteralBytes)
    {
        return std::unexpected(issue(ScenarioError::ResourceLimit, node.name));
    }
    if (!isLiteral(node.text))
    {
        return std::unexpected(issue(ScenarioError::InvalidLiteral, node.name));
    }
    return node.text;
}

std::expected<std::int32_t, ScenarioIssue> decimal(std::string_view text, std::string_view field)
{
    const auto digits = text.starts_with('-') ? text.substr(1) : text;
    if (digits.empty() || (digits.size() > 1 && digits.front() == '0') || text == "-0" ||
        !std::ranges::all_of(digits, [](char ch)
                             {
                                 return ch >= '0' && ch <= '9';
                             }))
    {
        return std::unexpected(issue(ScenarioError::NoncanonicalDecimal, field));
    }
    std::int32_t value  = 0;
    const auto   result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
    {
        return std::unexpected(issue(ScenarioError::NumericOutOfRange, field));
    }
    return value;
}

std::expected<ScenarioLiterals, ScenarioIssue> literals(const Element& node)
{
    if (const auto error = checkContainer(node))
    {
        return std::unexpected(*error);
    }
    ScenarioLiterals    result;
    std::array<bool, 5> seen{};
    for (const auto& child : node.children)
    {
        const auto found = std::ranges::find(kLocales, child.name);
        if (found == kLocales.end())
        {
            return std::unexpected(issue(ScenarioError::UnknownField, child.name));
        }
        const auto text = scalar(child);
        if (!text)
        {
            return std::unexpected(text.error());
        }
        const auto index = static_cast<std::size_t>(found - kLocales.begin());
        if (!seen[index])
        {
            result[index] = *text;
            seen[index]   = true;
        }
    }
    return result;
}

std::expected<void, ScenarioIssue> variator(const Element& node, ScenarioEntry& entry)
{
    if (const auto error = checkContainer(node))
    {
        return std::unexpected(*error);
    }
    bool seen = false;
    for (const auto& specs : node.children)
    {
        if (specs.name != "specs")
        {
            return std::unexpected(issue(ScenarioError::UnknownField, specs.name));
        }
        if (!specs.children.empty() || !isWhitespace(specs.text))
        {
            return std::unexpected(issue(ScenarioError::MalformedXml, "specs"));
        }
        std::optional<std::string_view> text;
        std::optional<std::string_view> level;
        for (const auto& attribute : specs.attributes)
        {
            if (attribute.name == "text")
            {
                text = attribute.value;
            }
            else if (attribute.name == "level")
            {
                level = attribute.value;
            }
            else
            {
                return std::unexpected(issue(ScenarioError::UnknownAttribute, attribute.name));
            }
        }
        if (!text || !level)
        {
            return std::unexpected(issue(ScenarioError::UnknownAttribute, !text ? "text" : "level"));
        }
        const auto found = std::ranges::find(kVariators, *text, &ScenarioVariator::name);
        if (found == kVariators.end())
        {
            return std::unexpected(issue(ScenarioError::UnknownVariator, *text));
        }
        const auto value = decimal(*level, *text);
        if (!value)
        {
            return std::unexpected(value.error());
        }
        if (*value < std::numeric_limits<std::int16_t>::min() || *value > std::numeric_limits<std::int16_t>::max())
        {
            return std::unexpected(issue(ScenarioError::NumericOutOfRange, *text));
        }
        if (!seen)
        {
            entry.variators[found->slot] = static_cast<std::int16_t>(*value);
            seen                         = true;
        }
    }
    // Missing specs was ignored by the game, but is not a complete canonical variator.
    if (!seen)
    {
        return std::unexpected(issue(ScenarioError::UnknownField, "specs"));
    }
    return {};
}

std::expected<ScenarioEntry, ScenarioIssue> parseEntry(const Element& node)
{
    if (const auto error = checkContainer(node))
    {
        return std::unexpected(*error);
    }
    ScenarioEntry       entry;
    std::array<bool, 5> seen{};
    for (const auto& child : node.children)
    {
        if (child.name == "VARIATOR")
        {
            const auto result = variator(child, entry);
            if (!result)
            {
                return std::unexpected(result.error());
            }
            continue;
        }
        constexpr std::array<std::string_view, 5> fields{ "MAP", "bMultiplayer", "ID", "TITLE", "DESCRIPTION" };
        const auto                                found = std::ranges::find(fields, child.name);
        if (found == fields.end())
        {
            return std::unexpected(issue(isExcluded(child.name) ? ScenarioError::ExcludedField : ScenarioError::UnknownField, child.name));
        }
        const auto index = static_cast<std::size_t>(found - fields.begin());
        if (index >= 3)
        {
            auto value = literals(child);
            if (!value)
            {
                return std::unexpected(value.error());
            }
            if (!seen[index])
            {
                (index == 3 ? entry.title : entry.description) = std::move(*value);
            }
        }
        else
        {
            const auto value = scalar(child);
            if (!value)
            {
                return std::unexpected(value.error());
            }
            if (index == 0)
            {
                if (!isMap(*value))
                {
                    return std::unexpected(issue(ScenarioError::MapNameUnproved, "MAP"));
                }
                if (!seen[index])
                {
                    entry.map = *value;
                }
            }
            else if (index == 1)
            {
                if (*value != "0" && *value != "1")
                {
                    return std::unexpected(issue(ScenarioError::NoncanonicalDecimal, "bMultiplayer"));
                }
                if (!seen[index])
                {
                    entry.multiplayer = *value == "1";
                }
            }
            else
            {
                const auto id = decimal(*value, "ID");
                if (!id)
                {
                    return std::unexpected(id.error());
                }
                if (!seen[index])
                {
                    entry.id = *id;
                }
            }
        }
        seen[index] = true;
    }
    return entry;
}

std::string encode(const ScenarioDocument& document)
{
    std::string xml = "<S>\n";
    for (const auto& entry : document.entries)
    {
        xml += "  <EntryInfo>\n    <MAP>" + entry.map + "</MAP>\n    <bMultiplayer>";
        xml += entry.multiplayer ? '1' : '0';
        xml += "</bMultiplayer>\n    <ID>" + std::to_string(entry.id) + "</ID>\n";
        const auto appendLiterals = [&](std::string_view name, const ScenarioLiterals& values)
        {
            if (std::ranges::all_of(values, [](const auto& value)
                                    {
                                        return value.empty();
                                    }))
            {
                return;
            }
            xml += "    <";
            xml += name;
            xml += ">\n";
            for (std::size_t i = 0; i < values.size(); ++i)
            {
                if (!values[i].empty())
                {
                    xml += "      <";
                    xml += kLocales[i];
                    xml += ">" + values[i] + "</";
                    xml += kLocales[i];
                    xml += ">\n";
                }
            }
            xml += "    </";
            xml += name;
            xml += ">\n";
        };
        appendLiterals("TITLE", entry.title);
        appendLiterals("DESCRIPTION", entry.description);
        for (const auto& descriptor : kVariators)
        {
            const auto level = entry.variators[descriptor.slot];
            if (level != descriptor.defaultLevel)
            {
                xml += "    <VARIATOR><specs text=\"";
                xml += descriptor.name;
                xml += "\" level=\"" + std::to_string(level) + "\"/></VARIATOR>\n";
            }
        }
        xml += "  </EntryInfo>\n";
    }
    xml += "</S>\n";
    return xml;
}

struct TemporaryFile
{
    std::filesystem::path path;

    ~TemporaryFile()
    {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
};

} // namespace

std::span<const ScenarioVariator> ScenarioVariators()
{
    return kVariators;
}

std::array<std::int16_t, 64> DefaultScenarioVariators()
{
    std::array<std::int16_t, 64> result{};
    for (const auto& descriptor : kVariators)
    {
        result[descriptor.slot] = descriptor.defaultLevel;
    }
    return result;
}

std::string_view ScenarioErrorMessage(ScenarioError error)
{
    switch (error)
    {
        case ScenarioError::ResourceLimit:
            return "The scenario exceeds a canonical reader/writer resource limit.";
        case ScenarioError::MapNameUnproved:
            return "MAP is NONE or up to 20 ASCII letters, digits and underscores, starting with a letter.";
        case ScenarioError::InvalidLiteral:
            return "Scenario literals require printable ASCII without entities, markup, or condensed whitespace.";
        case ScenarioError::DuplicateId:
            return "Scenario IDs must be unique. Duplicate IDs select the first appended record.";
        case ScenarioError::DuplicateMapName:
            return "Scenario MAP names must be unique after ASCII case folding because their derived basenames collide.";
        case ScenarioError::UnnamedVariatorSlot:
            return "The scenario changes a variator slot with no proved XML name.";
        case ScenarioError::UnknownField:
            return "The scenario contains a field outside the closed canonical subset.";
        case ScenarioError::ExcludedField:
            return "The scenario field is excluded because the bounded parser does not consume it here.";
        case ScenarioError::UnknownAttribute:
            return "The scenario contains an unknown attribute or lacks a required specs attribute.";
        case ScenarioError::UnknownVariator:
            return "The scenario variator name must match a name in the closed "
                   "namespace exactly, including case.";
        case ScenarioError::NoncanonicalDecimal:
            return "The scenario requires canonical ASCII decimal without prefixes, wrap, or trailing text.";
        case ScenarioError::NumericOutOfRange:
            return "The scenario integer exceeds its signed storage range.";
        case ScenarioError::EmptyScalar:
            return "Empty scenario scalar elements are outside the admitted subset.";
        case ScenarioError::MalformedXml:
            return "The scenario XML is malformed or has mixed content.";
        case ScenarioError::UnsupportedXml:
            return "The scenario XML uses constructs outside the plain canonical subset.";
        case ScenarioError::DestinationExists:
            return "The scenario destination already exists.";
        case ScenarioError::OpenFailed:
            return "Could not create a temporary scenario file beside the destination.";
        case ScenarioError::WriteFailed:
            return "Could not write the complete scenario XML file.";
        case ScenarioError::FinalizationFailed:
            return "Could not finalize the new scenario file. The destination "
                   "volume must support hard links.";
        case ScenarioError::ReadBackFailed:
            return "Strict scenario readback did not match the document and XML bytes exactly.";
    }
    return "The scenario operation failed.";
}

std::vector<ScenarioIssue> ValidateScenarioDocument(const ScenarioDocument& document)
{
    std::vector<ScenarioIssue> issues;
    if (document.entries.size() > kScenarioMaximumEntries)
    {
        issues.push_back(issue(ScenarioError::ResourceLimit, "EntryInfo"));
    }
    std::set<std::int32_t> ids;
    std::set<std::string>  mapNames;
    // Cap the size calculation before allocating the output string.
    std::size_t size    = 9;
    const auto  addSize = [&](std::size_t amount)
    {
        size += std::min(amount, kScenarioMaximumXmlBytes + 1 - std::min(size, kScenarioMaximumXmlBytes + 1));
    };
    const auto defaults = DefaultScenarioVariators();
    for (std::size_t index = 0; index < document.entries.size(); ++index)
    {
        const auto& entry = document.entries[index];
        if (!isMap(entry.map))
        {
            issues.push_back(issue(ScenarioError::MapNameUnproved, "MAP", index));
        }
        else if (entry.map != "NONE")
        {
            std::string folded = entry.map;
            std::ranges::transform(folded, folded.begin(), [](char ch)
                                   {
                                       return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch + ('a' - 'A')) : ch;
                                   });
            if (!mapNames.emplace(std::move(folded)).second)
            {
                issues.push_back(issue(ScenarioError::DuplicateMapName, "MAP", index));
            }
        }
        if (!ids.emplace(entry.id).second)
        {
            issues.push_back(issue(ScenarioError::DuplicateId, "ID", index));
        }
        addSize(std::string_view("  <EntryInfo>\n    <MAP></MAP>\n    <bMultiplayer>0</bMultiplayer>\n    <ID></ID>\n  </EntryInfo>\n").size());
        addSize(entry.map.size());
        addSize(std::to_string(entry.id).size());
        for (std::size_t group = 0; group < 2; ++group)
        {
            const auto& values = group == 0 ? entry.title : entry.description;
            if (std::ranges::any_of(values, [](const auto& value)
                                    {
                                        return !value.empty();
                                    }))
            {
                addSize(group == 0 ? std::string_view("    <TITLE>\n    </TITLE>\n").size() : std::string_view("    <DESCRIPTION>\n    </DESCRIPTION>\n").size());
            }
            for (std::size_t locale = 0; locale < values.size(); ++locale)
            {
                const std::string field = std::string(group == 0 ? "TITLE/" : "DESCRIPTION/") + std::string(kLocales[locale]);
                if (!isLiteral(values[locale]))
                {
                    issues.push_back(issue(ScenarioError::InvalidLiteral, field, index));
                }
                if (values[locale].size() > kScenarioMaximumLiteralBytes)
                {
                    issues.push_back(issue(ScenarioError::ResourceLimit, field, index));
                }
                if (!values[locale].empty())
                {
                    addSize(std::string_view("      <ENG></ENG>\n").size());
                    addSize(values[locale].size());
                }
            }
        }
        for (std::size_t slot = 0; slot < entry.variators.size(); ++slot)
        {
            const auto found = std::ranges::find(kVariators, slot, &ScenarioVariator::slot);
            if (found == kVariators.end() && entry.variators[slot] != defaults[slot])
            {
                issues.push_back(issue(ScenarioError::UnnamedVariatorSlot, "VARIATOR/" + std::to_string(slot), index));
            }
            if (entry.variators[slot] != defaults[slot])
            {
                addSize(std::string_view("    <VARIATOR><specs text=\"\" level=\"\"/></VARIATOR>\n").size());
                if (found != kVariators.end())
                {
                    addSize(found->name.size());
                }
                addSize(std::to_string(entry.variators[slot]).size());
            }
        }
    }
    if (size > kScenarioMaximumXmlBytes)
    {
        issues.push_back(issue(ScenarioError::ResourceLimit, "XML"));
    }
    return issues;
}

std::string_view SelectScenarioLiteral(const ScenarioLiterals& literals, std::int32_t localeId)
{
    switch (localeId)
    {
        case 14:
            return literals[1];
        case 18:
            return literals[3];
        case 24:
            return literals[2];
        case 37:
            return literals[4];
        default:
            return literals[0];
    }
}

std::size_t ScenarioEnumerationCount(const ScenarioDocument& document, bool multiplayer)
{
    if (!multiplayer)
    {
        return 10 + document.entries.size();
    }
    return static_cast<std::size_t>(std::ranges::count(document.entries, true, &ScenarioEntry::multiplayer));
}

std::expected<std::optional<std::array<std::string, 4>>, ScenarioIssue> ScenarioMapFiles(const ScenarioEntry& entry)
{
    if (!isMap(entry.map))
    {
        return std::unexpected(issue(ScenarioError::MapNameUnproved, "MAP"));
    }
    if (entry.map == "NONE")
    {
        return std::optional<std::array<std::string, 4>>{};
    }
    return std::optional{ std::array<std::string, 4>{ entry.map + ".map", "Map" + entry.map + "_Heights.dds", "Map" + entry.map + "_MountainHill_Blends.dds", "Map" + entry.map + "_Lightmap.dds" } };
}

std::expected<ScenarioDocument, ScenarioIssue> ParseScenarioDocument(std::span<const std::byte> bytes)
{
    if (bytes.size() > kScenarioMaximumXmlBytes)
    {
        return std::unexpected(issue(ScenarioError::ResourceLimit, "XML"));
    }
    if (bytes.empty())
    {
        return std::unexpected(issue(ScenarioError::MalformedXml));
    }
    const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (!std::ranges::all_of(text, [](unsigned char ch)
                             {
                                 return (ch >= 0x20 && ch <= 0x7E) || isWhitespace(static_cast<char>(ch));
                             }))
    {
        return std::unexpected(issue(ScenarioError::UnsupportedXml));
    }
    auto root = XmlReader(text).read();
    if (!root)
    {
        return std::unexpected(root.error());
    }
    if (const auto error = checkContainer(*root))
    {
        return std::unexpected(*error);
    }
    ScenarioDocument document;
    for (const auto& child : root->children)
    {
        if (child.name != "EntryInfo")
        {
            return std::unexpected(issue(isExcluded(child.name) ? ScenarioError::ExcludedField : ScenarioError::UnknownField, child.name));
        }
        if (document.entries.size() == kScenarioMaximumEntries)
        {
            return std::unexpected(issue(ScenarioError::ResourceLimit, "EntryInfo"));
        }
        auto entry = parseEntry(child);
        if (!entry)
        {
            auto error       = entry.error();
            error.entryIndex = document.entries.size();
            return std::unexpected(std::move(error));
        }
        document.entries.push_back(std::move(*entry));
    }
    const auto issues = ValidateScenarioDocument(document);
    if (!issues.empty())
    {
        return std::unexpected(issues.front());
    }
    return document;
}

std::expected<std::vector<std::byte>, ScenarioIssue> EncodeScenarioXml(const ScenarioDocument& document)
{
    const auto issues = ValidateScenarioDocument(document);
    if (!issues.empty())
    {
        return std::unexpected(issues.front());
    }
    const auto xml   = encode(document);
    const auto bytes = std::as_bytes(std::span(xml.data(), xml.size()));
    return std::vector<std::byte>(bytes.begin(), bytes.end());
}

std::expected<void, ScenarioIssue> WriteScenarioXml(const ScenarioDocument& document, const std::filesystem::path& destination)
{
    const auto bytes = EncodeScenarioXml(document);
    if (!bytes)
    {
        return std::unexpected(bytes.error());
    }
    std::error_code error;
    const auto      destinationStatus = std::filesystem::symlink_status(destination, error);
    if (destinationStatus.type() != std::filesystem::file_type::not_found && !error)
    {
        return std::unexpected(issue(ScenarioError::DestinationExists));
    }
    if (error && error != std::errc::no_such_file_or_directory)
    {
        return std::unexpected(issue(ScenarioError::OpenFailed));
    }
    static std::atomic<std::uint64_t> sequence{ 0 };
    std::ofstream                     output;
    std::filesystem::path             temporary;
    for (unsigned attempt = 0; attempt < 128; ++attempt)
    {
        temporary = destination;
        temporary += ".tmp-" + std::to_string(sequence.fetch_add(1));
        output.open(temporary, std::ios::binary | std::ios::noreplace);
        if (output)
        {
            break;
        }
        error.clear();
        const auto status = std::filesystem::symlink_status(temporary, error);
        if (error || status.type() == std::filesystem::file_type::not_found)
        {
            return std::unexpected(issue(ScenarioError::OpenFailed));
        }
        output.clear();
    }
    if (!output.is_open())
    {
        return std::unexpected(issue(ScenarioError::OpenFailed));
    }
    const TemporaryFile cleanup{ temporary };
    output.write(reinterpret_cast<const char*>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
    if (!output)
    {
        output.close();
        return std::unexpected(issue(ScenarioError::WriteFailed));
    }
    output.close();
    if (output.fail())
    {
        return std::unexpected(issue(ScenarioError::WriteFailed));
    }
    std::ifstream          input(temporary, std::ios::binary);
    std::vector<std::byte> readBackBytes(bytes->size());
    input.read(reinterpret_cast<char*>(readBackBytes.data()), static_cast<std::streamsize>(readBackBytes.size()));
    if (!input || input.peek() != std::char_traits<char>::eof() || input.bad() || readBackBytes != *bytes)
    {
        return std::unexpected(issue(ScenarioError::ReadBackFailed));
    }
    const auto readBack = ParseScenarioDocument(readBackBytes);
    if (!readBack || *readBack != document)
    {
        return std::unexpected(issue(ScenarioError::ReadBackFailed));
    }
    input.close();
    // Hard link publication is atomic and refuses an existing destination.
    error.clear();
    std::filesystem::create_hard_link(temporary, destination, error);
    if (error)
    {
        std::error_code statusError;
        const auto      status = std::filesystem::symlink_status(destination, statusError);
        if (!statusError && status.type() != std::filesystem::file_type::not_found)
        {
            return std::unexpected(issue(ScenarioError::DestinationExists));
        }
        return std::unexpected(issue(ScenarioError::FinalizationFailed));
    }
    return {};
}

} // namespace rerevved::tools
