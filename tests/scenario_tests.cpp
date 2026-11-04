#include "scenario_document.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <span>
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

std::span<const std::byte> bytes(std::string_view text)
{
    return std::as_bytes(std::span(text.data(), text.size()));
}

std::string text(const std::vector<std::byte>& data)
{
    return { reinterpret_cast<const char*>(data.data()), data.size() };
}

void expectError(std::string_view xml, ScenarioError error, std::string_view field)
{
    const auto parsed = ParseScenarioDocument(bytes(xml));
    expect(!parsed && parsed.error().code == error && parsed.error().field == field, xml);
}

ScenarioDocument canonicalDocument()
{
    ScenarioEntry entry;
    entry.map           = "A";
    entry.multiplayer   = true;
    entry.id            = 42;
    entry.title         = { "Original title", "Titre original", "Titolo originale", "Eigener Titel", "Titulo original" };
    entry.description   = { "An original world.", "Un monde original.", "Un mondo originale.", "Eine eigene Welt.", "Un mundo original." };
    entry.variators[4]  = 3;
    entry.variators[12] = -32768;
    entry.variators[14] = -2;
    entry.variators[30] = -7;
    entry.variators[31] = -4000;
    entry.variators[32] = -3;
    entry.variators[33] = -4;
    entry.variators[34] = -5;
    entry.variators[35] = -6;
    entry.variators[36] = -7;
    entry.variators[37] = -8;
    entry.variators[38] = 32767;
    return { { entry, ScenarioEntry{} } };
}

constexpr std::string_view kCanonicalXml =
    "<S>\n"
    "  <EntryInfo>\n"
    "    <MAP>A</MAP>\n"
    "    <bMultiplayer>1</bMultiplayer>\n"
    "    <ID>42</ID>\n"
    "    <TITLE>\n"
    "      <ENG>Original title</ENG>\n"
    "      <FRA>Titre original</FRA>\n"
    "      <ITA>Titolo originale</ITA>\n"
    "      <DEU>Eigener Titel</DEU>\n"
    "      <ESP>Titulo original</ESP>\n"
    "    </TITLE>\n"
    "    <DESCRIPTION>\n"
    "      <ENG>An original world.</ENG>\n"
    "      <FRA>Un monde original.</FRA>\n"
    "      <ITA>Un mondo originale.</ITA>\n"
    "      <DEU>Eine eigene Welt.</DEU>\n"
    "      <ESP>Un mundo original.</ESP>\n"
    "    </DESCRIPTION>\n"
    "    <VARIATOR><specs text=\"TECHNOLOGYRATE\" level=\"3\"/></VARIATOR>\n"
    "    <VARIATOR><specs text=\"SPEEDMODE\" level=\"-32768\"/></VARIATOR>\n"
    "    <VARIATOR><specs text=\"STARTERA\" level=\"-2\"/></VARIATOR>\n"
    "    <VARIATOR><specs text=\"DISPLAYCARD\" level=\"-7\"/></VARIATOR>\n"
    "    <VARIATOR><specs text=\"STARTYEAR\" level=\"-4000\"/></VARIATOR>\n"
    "    <VARIATOR><specs text=\"MAPNUMBER\" level=\"-3\"/></VARIATOR>\n"
    "    <VARIATOR><specs text=\"STARTLOCME\" level=\"-4\"/></VARIATOR>\n"
    "    <VARIATOR><specs text=\"STARTLOC0\" level=\"-5\"/></VARIATOR>\n"
    "    <VARIATOR><specs text=\"STARTLOC1\" level=\"-6\"/></VARIATOR>\n"
    "    <VARIATOR><specs text=\"STARTLOC2\" level=\"-7\"/></VARIATOR>\n"
    "    <VARIATOR><specs text=\"STARTLOC3\" level=\"-8\"/></VARIATOR>\n"
    "    <VARIATOR><specs text=\"STARTLOC4\" level=\"32767\"/></VARIATOR>\n"
    "  </EntryInfo>\n"
    "  <EntryInfo>\n"
    "    <MAP>NONE</MAP>\n"
    "    <bMultiplayer>0</bMultiplayer>\n"
    "    <ID>-1</ID>\n"
    "  </EntryInfo>\n"
    "</S>\n";

void testCanonicalRoundTrip()
{
    const auto document = canonicalDocument();
    expect(ValidateScenarioDocument(document).empty(), "canonical model validates");
    const auto encoded = EncodeScenarioXml(document);
    expect(encoded && text(*encoded) == kCanonicalXml, "canonical XML is byte-exact pinned output");
    if (!encoded)
    {
        return;
    }
    const auto original = *encoded;
    const auto parsed   = ParseScenarioDocument(*encoded);
    expect(parsed && *parsed == document, "canonical read-back compares the complete model");
    expect(*encoded == original, "reader preserves its source span");
    if (parsed)
    {
        const auto repeated = EncodeScenarioXml(*parsed);
        expect(repeated && *repeated == original, "parsed model re-encodes to identical bytes");
    }
}

void testXboxDefaultsAndAppend()
{
    const std::array<std::int16_t, 64> defaults{
        1, 1, 1, 2, 1, 1, 1, 2, 1, 1, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };
    expect(DefaultScenarioVariators() == defaults, "all 64 Xbox slots match pinned defaults");
    const auto minimal = ParseScenarioDocument(bytes("<ArbitraryRoot><EntryInfo/></ArbitraryRoot>"));
    expect(minimal && minimal->entries.size() == 1 && minimal->entries.front() == ScenarioEntry{}, "empty direct EntryInfo appends a default record under arbitrary root");
    const auto none = ParseScenarioDocument(bytes("<Root/>"));
    expect(none && none->entries.empty(), "root without records appends nothing");
    const auto ordered = ParseScenarioDocument(bytes("<S><EntryInfo><ID>9</ID></EntryInfo><EntryInfo><ID>-1</ID><bMultiplayer>1</bMultiplayer></EntryInfo><EntryInfo><ID>3</ID></EntryInfo></S>"));
    expect(ordered && ordered->entries.size() == 3 && ordered->entries[0].id == 9 && ordered->entries[1].id == -1 && ordered->entries[2].id == 3, "records retain append order including signed default ID");
    if (ordered)
    {
        expect(ScenarioEnumerationCount(*ordered, false) == 13, "ordinary enumeration includes ten built-ins");
        expect(ScenarioEnumerationCount(*ordered, true) == 1, "multiplayer enumeration filters the appended records");
    }
    expect(ScenarioEnumerationCount(ScenarioDocument{}, false) == 10 && ScenarioEnumerationCount(ScenarioDocument{}, true) == 0, "empty enumeration counts differ by filter");
}

void testAllSignedVariators()
{
    ScenarioDocument document{ { ScenarioEntry{} } };
    expect(ScenarioVariators().size() == 35, "35 names are exposed");
    std::array<bool, 64> seen{};
    for (const auto& descriptor : ScenarioVariators())
    {
        expect(descriptor.slot < 64 && !seen[descriptor.slot], "variator slots are unique and bounded");
        if (descriptor.slot >= 64)
        {
            continue;
        }
        seen[descriptor.slot]                          = true;
        document.entries[0].variators[descriptor.slot] = std::numeric_limits<std::int16_t>::min();
    }
    const auto encoded = EncodeScenarioXml(document);
    const auto parsed  = encoded ? ParseScenarioDocument(*encoded) : std::expected<ScenarioDocument, ScenarioIssue>(std::unexpected(encoded.error()));
    expect(parsed && *parsed == document, "every named signed slot preserves -32768");
    for (auto& value : document.entries[0].variators)
    {
        if (value == std::numeric_limits<std::int16_t>::min())
        {
            value = std::numeric_limits<std::int16_t>::max();
        }
    }
    document.entries[0].id = std::numeric_limits<std::int32_t>::min();
    const auto upperBytes  = EncodeScenarioXml(document);
    expect(upperBytes.has_value(), "signed upper slot limits encode");
    if (upperBytes)
    {
        const auto upper = ParseScenarioDocument(*upperBytes);
        expect(upper && *upper == document, "32767 levels and -2147483648 ID round-trip");
    }
    const auto duplicates = ParseScenarioDocument(bytes("<S><EntryInfo><ID>5</ID><ID>8</ID><TITLE><ENG>First</ENG><ENG>Second</ENG></TITLE><TITLE><ENG>Third</ENG></TITLE><VARIATOR><specs text=\"SPEEDMODE\" level=\"1\"/><specs text=\"SPEEDMODE\" level=\"2\"/></VARIATOR><VARIATOR><specs text=\"SPEEDMODE\" level=\"-3\"/></VARIATOR></EntryInfo></S>"));
    expect(duplicates && duplicates->entries[0].id == 5 && duplicates->entries[0].title[0] == "First" && duplicates->entries[0].variators[12] == -3, "first scalar/locale/specs and last repeated VARIATOR match parser order");
}

void testLocalesAndMapHandoff()
{
    const ScenarioLiterals values{ "English", "French", "Italian", "German", "Spanish" };
    expect(SelectScenarioLiteral(values, 14) == "French" && SelectScenarioLiteral(values, 18) == "German" && SelectScenarioLiteral(values, 24) == "Italian" && SelectScenarioLiteral(values, 37) == "Spanish", "literal locale ID mapping is exact");
    expect(SelectScenarioLiteral(values, -1) == "English" && SelectScenarioLiteral(values, 0) == "English", "other locale IDs select ENG");
    const ScenarioLiterals englishOnly{ "English", "", "", "", "" };
    expect(SelectScenarioLiteral(englishOnly, 14).empty(), "non-English locale does not fall back");
    const auto parsed = ParseScenarioDocument(bytes("<S><EntryInfo><TITLE><ENG>Literal title</ENG></TITLE><DESCRIPTION><ESP>Literal description</ESP></DESCRIPTION></EntryInfo></S>"));
    expect(parsed && parsed->entries[0].title[0] == "Literal title" && parsed->entries[0].title[1].empty() && parsed->entries[0].description[4] == "Literal description", "missing locale children remain empty");
    const auto                       files = ScenarioMapFiles(canonicalDocument().entries[0]);
    const std::array<std::string, 4> expected{ "A.map", "MapA_Heights.dds", "MapA_MountainHill_Blends.dds", "MapA_Lightmap.dds" };
    expect(files && *files && **files == expected, "named map uses all four exact derived basenames");
    const auto none = ScenarioMapFiles(ScenarioEntry{});
    expect(none && !*none, "NONE suppresses named map handoff");
    ScenarioEntry lowercase;
    lowercase.map         = "a";
    const auto lowerFiles = ScenarioMapFiles(lowercase);
    expect(lowerFiles && *lowerFiles && (**lowerFiles)[0] == "a.map", "MAP case is preserved without normalization");
}

void testMapNameCollisions()
{
    expect(ScenarioErrorMessage(ScenarioError::DuplicateMapName) == "Scenario MAP names must be unique after ASCII case folding because their derived basenames collide.", "duplicate-MAP diagnostic identifies derived basename collisions");
    ScenarioDocument document{ { ScenarioEntry{}, ScenarioEntry{} } };
    document.entries[0].id = 1;
    document.entries[1].id = 2;
    for (const auto& names : { std::array{ "A", "a" }, std::array{ "a", "A" }, std::array{ "A", "A" } })
    {
        document.entries[0].map = names[0];
        document.entries[1].map = names[1];
        const auto issues       = ValidateScenarioDocument(document);
        expect(issues.size() == 1 && issues[0].code == ScenarioError::DuplicateMapName && issues[0].entryIndex == 1 && issues[0].field == "MAP", "folded MAP collision identifies the later record");
        const auto encoded = EncodeScenarioXml(document);
        expect(!encoded && encoded.error().code == ScenarioError::DuplicateMapName, "colliding MAP-derived basenames cannot encode");
        expect(document.entries[0].map == names[0] && document.entries[1].map == names[1], "collision validation preserves MAP case");
        const std::string xml = "<S><EntryInfo><ID>1</ID><MAP>" + document.entries[0].map + "</MAP></EntryInfo><EntryInfo><ID>2</ID><MAP>" + document.entries[1].map + "</MAP></EntryInfo></S>";
        expectError(xml, ScenarioError::DuplicateMapName, "MAP");
    }
    document.entries[0].map = "A";
    document.entries[1].map = "B";
    expect(ValidateScenarioDocument(document).empty(), "distinct named-map basenames validate");
    document.entries[0].map = "rr_pangaea";
    document.entries[1].map = "Mountains_of_Madness";
    expect(ValidateScenarioDocument(document).empty(), "names up to 20 characters validate");
    document.entries[1].map = "RR_Pangaea";
    expect(ValidateScenarioDocument(document).size() == 1, "long MAP names collide after case folding");
    document.entries[0].map = "NONE";
    document.entries[1].map = "NONE";
    expect(ValidateScenarioDocument(document).empty(), "repeated NONE has no named-map basename collision");
}

void testValidationReportsAllIssues()
{
    expect(ScenarioErrorMessage(ScenarioError::DuplicateId) == "Scenario IDs must be unique. Duplicate IDs select the first appended record.", "duplicate-ID diagnostic preserves the selection reason");
    expect(ScenarioErrorMessage(ScenarioError::MapNameUnproved) == "MAP is NONE or up to 20 ASCII letters, digits and underscores, starting with a letter.", "MAP diagnostic names the accepted names");
    expect(ScenarioErrorMessage(ScenarioError::ReadBackFailed) == "Strict scenario readback did not match the document and XML bytes exactly.", "read-back diagnostic identifies both comparisons");
    ScenarioDocument document{ { ScenarioEntry{}, ScenarioEntry{} } };
    document.entries[0].map            = "Long-Name";
    document.entries[0].title[0]       = "Unproved & literal";
    document.entries[1].description[4] = " leading space";
    document.entries[0].variators[26]  = -1;
    document.entries[1].variators[63]  = 1;
    const auto issues                  = ValidateScenarioDocument(document);
    expect(issues.size() == 6, "validation reports all independent model defects");
    const auto hasIssue = [&](ScenarioError code, std::size_t index, std::string_view field)
    {
        return std::ranges::any_of(issues, [&](const ScenarioIssue& value)
                                   {
                                       return value.code == code && value.entryIndex == index && value.field == field;
                                   });
    };
    expect(hasIssue(ScenarioError::MapNameUnproved, 0, "MAP") && hasIssue(ScenarioError::DuplicateId, 1, "ID") &&
               hasIssue(ScenarioError::InvalidLiteral, 0, "TITLE/ENG") && hasIssue(ScenarioError::InvalidLiteral, 1, "DESCRIPTION/ESP") &&
               hasIssue(ScenarioError::UnnamedVariatorSlot, 0, "VARIATOR/26") && hasIssue(ScenarioError::UnnamedVariatorSlot, 1, "VARIATOR/63"),
           "every issue identifies its record and field");
    expect(!EncodeScenarioXml(document), "invalid models cannot encode");
    expectError("<S><EntryInfo/><EntryInfo/></S>", ScenarioError::DuplicateId, "ID");
    expectError("<S><EntryInfo><ID>7</ID></EntryInfo><EntryInfo><ID>7</ID></EntryInfo></S>", ScenarioError::DuplicateId, "ID");
}

void testUnknownAndExcludedInputs()
{
    constexpr std::array excluded{ "EntryType", "EntryTag", "MAPNUMBER", "STARTLOCME", "STARTLOC0", "STARTLOC1", "STARTLOC2", "STARTLOC3", "STARTLOC4" };
    for (const auto field : excluded)
    {
        const std::string xml = "<S><EntryInfo><" + std::string(field) + ">1</" + field + "></EntryInfo></S>";
        expectError(xml, ScenarioError::ExcludedField, field);
    }
    constexpr std::array unknown{ "SimulationSpeed", "TechnologyEraMembership", "GrowthRate", "CalendarRate", "Entryinfo", "map", "SPEEDMODE", "STARTERA" };
    for (const auto field : unknown)
    {
        const std::string xml = "<S><EntryInfo><" + std::string(field) + ">1</" + field + "></EntryInfo></S>";
        expectError(xml, ScenarioError::UnknownField, field);
    }
    constexpr std::array unknownVariators{ "UNKNOWN", "speedmode", "EntryType", "EntryTag", "SIMULATIONSPEED", "TECHNOLOGYERAMEMBERSHIP" };
    for (const auto field : unknownVariators)
    {
        const std::string xml = "<S><EntryInfo><VARIATOR><specs text=\"" + std::string(field) + "\" level=\"1\"/></VARIATOR></EntryInfo></S>";
        expectError(xml, ScenarioError::UnknownVariator, field);
    }
    expectError("<S extra=\"1\"/>", ScenarioError::UnknownAttribute, "extra");
    expectError("<S><EntryInfo extra=\"1\"/></S>", ScenarioError::UnknownAttribute, "extra");
    expectError("<S><EntryInfo><MAP extra=\"1\">A</MAP></EntryInfo></S>", ScenarioError::UnknownAttribute, "extra");
    expectError("<S><EntryInfo><TITLE><JPN>Literal</JPN></TITLE></EntryInfo></S>", ScenarioError::UnknownField, "JPN");
    expectError("<S><EntryInfo><VARIATOR><specs text=\"SPEEDMODE\"/></VARIATOR></EntryInfo></S>", ScenarioError::UnknownAttribute, "level");
    expectError("<S><EntryInfo><VARIATOR><specs level=\"1\"/></VARIATOR></EntryInfo></S>", ScenarioError::UnknownAttribute, "text");
    expectError("<S><EntryInfo><VARIATOR><specs text=\"SPEEDMODE\" level=\"1\" other=\"1\"/></VARIATOR></EntryInfo></S>", ScenarioError::UnknownAttribute, "other");
    expectError("<S><EntryInfo><VARIATOR/></EntryInfo></S>", ScenarioError::UnknownField, "specs");
    expectError("<S><EntryInfo><TITLE><ENG>Literal &amp; entity</ENG></TITLE></EntryInfo></S>", ScenarioError::InvalidLiteral, "ENG");
    for (const auto map : { "none", "None", "A-B", "_A", "A B", "Mountains_of_Madness1", "A/B", "A\\B", ".", "1" })
    {
        const std::string xml = "<S><EntryInfo><MAP>" + std::string(map) + "</MAP></EntryInfo></S>";
        expectError(xml, ScenarioError::MapNameUnproved, "MAP");
    }
    for (const auto number : { "+1", "01", "-0", "1tail", " 1", "1 " })
    {
        const std::string xml    = "<S><EntryInfo><ID>" + std::string(number) + "</ID></EntryInfo></S>";
        const auto        result = ParseScenarioDocument(bytes(xml));
        expect(!result, "noncanonical decimal never depends on retail prefix/wrap/whitespace behavior");
    }
    expectError("<S><EntryInfo><ID>2147483648</ID></EntryInfo></S>", ScenarioError::NumericOutOfRange, "ID");
    expectError("<S><EntryInfo><VARIATOR><specs text=\"SPEEDMODE\" level=\"32768\"/></VARIATOR></EntryInfo></S>", ScenarioError::NumericOutOfRange, "SPEEDMODE");
    expectError("<S><EntryInfo><VARIATOR><specs text=\"STARTERA\" level=\"-32769\"/></VARIATOR></EntryInfo></S>", ScenarioError::NumericOutOfRange, "STARTERA");
    expectError("<S><EntryInfo><bMultiplayer>10</bMultiplayer></EntryInfo></S>", ScenarioError::NoncanonicalDecimal, "bMultiplayer");
    expectError("<S><EntryInfo><ID>1</ID><ID>01</ID></EntryInfo></S>", ScenarioError::NoncanonicalDecimal, "ID");
    expectError("<S><EntryInfo><TITLE><ENG>First</ENG><ENG> Leading</ENG></TITLE></EntryInfo></S>", ScenarioError::InvalidLiteral, "ENG");
    expectError("<S><EntryInfo><TITLE/><TITLE><JPN>Later</JPN></TITLE></EntryInfo></S>", ScenarioError::UnknownField, "JPN");
    expectError("<S><EntryInfo><VARIATOR><specs text=\"SPEEDMODE\" level=\"1\"/><specs text=\"UNKNOWN\" level=\"1\"/></VARIATOR></EntryInfo></S>", ScenarioError::UnknownVariator, "UNKNOWN");
    expectError("<S><EntryInfo><VARIATOR><specs text=\"SPEEDMODE\" level=\"1\"/><specs text=\"SPEEDMODE\" level=\"01\"/></VARIATOR></EntryInfo></S>", ScenarioError::NoncanonicalDecimal, "SPEEDMODE");
    expectError("<S><EntryInfo><TITLE><ENG>caf\xC3\xA9</ENG></TITLE></EntryInfo></S>", ScenarioError::UnsupportedXml, "");
    expectError("<S xmlns=\"urn:scenario\"/>", ScenarioError::UnknownAttribute, "xmlns");
    expectError("<S><x:EntryInfo/></S>", ScenarioError::MalformedXml, "x");
}

void testMalformedAndBoundedReader()
{
    constexpr std::array malformed{
        "", "<S>", "<S><EntryInfo></S>", "<S/><S/>", "<S>trailing</S>", "<S><EntryInfo><ID>1</ID>mixed</EntryInfo></S>", "<S><EntryInfo><MAP>A</MAP></EntryInfo></S>trailing", "<S><EntryInfo><VARIATOR><specs text=\"SPEEDMODE\" text=\"SPEEDMODE\"/></VARIATOR></EntryInfo></S>", "<S><EntryInfo><VARIATOR><specs text=\"SPEEDMODE\"level=\"1\"/></VARIATOR></EntryInfo></S>", "<?xml version=\"1.0\"?><S/>", "<!DOCTYPE S><S/>", "<S><!--comment--></S>", "<S><EntryInfo><TITLE><ENG><![CDATA[literal]]></ENG></TITLE></EntryInfo></S>"
    };
    for (const auto xml : malformed)
    {
        expect(!ParseScenarioDocument(bytes(xml)), xml);
    }
    for (const auto field : { "MAP", "ID", "bMultiplayer" })
    {
        const std::string xml = "<S><EntryInfo><" + std::string(field) + "/></EntryInfo></S>";
        expectError(xml, ScenarioError::EmptyScalar, field);
    }
    expectError("<S><EntryInfo><TITLE><ENG/></TITLE></EntryInfo></S>", ScenarioError::EmptyScalar, "ENG");
    expectError("<S><EntryInfo><TITLE><ENG><nested/></ENG></TITLE></EntryInfo></S>", ScenarioError::ResourceLimit, "");
    std::string nul = "<S/>";
    nul.push_back('\0');
    expectError(nul, ScenarioError::UnsupportedXml, "");
    std::string manyElements = "<S><EntryInfo>";
    for (std::size_t i = 0; i < 65535; ++i)
    {
        manyElements += "<MAP/>";
    }
    manyElements += "</EntryInfo></S>";
    expectError(manyElements, ScenarioError::ResourceLimit, "");
    expectError(std::string(kScenarioMaximumXmlBytes + 1, ' '), ScenarioError::ResourceLimit, "XML");
    ScenarioDocument oversized{ { ScenarioEntry{} } };
    oversized.entries[0].title[0] = std::string(kScenarioMaximumLiteralBytes + 1, 'a');
    const auto issues             = ValidateScenarioDocument(oversized);
    expect(issues.size() == 1 && issues[0].code == ScenarioError::ResourceLimit, "literal allocation is bounded");
    const std::string longLiteral = "<S><EntryInfo><TITLE><ENG>" + std::string(kScenarioMaximumLiteralBytes + 1, 'a') + "</ENG></TITLE></EntryInfo></S>";
    expectError(longLiteral, ScenarioError::ResourceLimit, "ENG");
    ScenarioDocument maximumEntries;
    for (std::size_t i = 0; i < kScenarioMaximumEntries; ++i)
    {
        ScenarioEntry entry;
        entry.id = static_cast<std::int32_t>(i);
        maximumEntries.entries.push_back(entry);
    }
    const auto maximumBytes = EncodeScenarioXml(maximumEntries);
    expect(maximumBytes.has_value(), "maximum canonical entry count encodes within the byte bound");
    if (maximumBytes)
    {
        const auto maximumParsed = ParseScenarioDocument(*maximumBytes);
        expect(maximumParsed && *maximumParsed == maximumEntries, "maximum canonical entry count reads back under the same limits");
    }
    ScenarioDocument largeXml;
    for (std::int32_t i = 0; i < 26; ++i)
    {
        ScenarioEntry entry;
        entry.id = i;
        entry.title.fill(std::string(kScenarioMaximumLiteralBytes, 'a'));
        entry.description = entry.title;
        largeXml.entries.push_back(entry);
    }
    const auto largeIssues = ValidateScenarioDocument(largeXml);
    expect(largeIssues.size() == 1 && largeIssues[0].code == ScenarioError::ResourceLimit && largeIssues[0].field == "XML", "model validation bounds total encoded bytes before encoding");
    std::string many = "<S>";
    for (std::size_t i = 0; i <= kScenarioMaximumEntries; ++i)
    {
        many += "<EntryInfo><ID>" + std::to_string(i) + "</ID></EntryInfo>";
    }
    many += "</S>";
    expectError(many, ScenarioError::ResourceLimit, "EntryInfo");
    for (std::size_t length = 0; length < kCanonicalXml.size() - 1; ++length)
    {
        expect(!ParseScenarioDocument(bytes(kCanonicalXml.substr(0, length))), "truncated canonical document is rejected");
    }
}

void testFileWriter(const std::filesystem::path& scratch)
{
    const auto      document    = canonicalDocument();
    const auto      destination = scratch / "scenario-round-trip.xml";
    const auto      sibling     = scratch / "scenario-round-trip.xml.tmp-0";
    std::error_code error;
    {
        std::ofstream sentinel(sibling, std::ios::binary | std::ios::noreplace);
        expect(sentinel.is_open(), "create caller-owned temporary-name sentinel");
        sentinel << "caller sentinel";
    }
    const auto written = WriteScenarioXml(document, destination);
    expect(written.has_value(), "writer verifies and publishes a fresh destination");
    {
        std::ifstream     input(destination, std::ios::binary);
        const std::string content{ std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
        expect(content == kCanonicalXml, "published bytes match the pinned XML");
    }
    const auto refused = WriteScenarioXml(ScenarioDocument{}, destination);
    expect(!refused && refused.error().code == ScenarioError::DestinationExists, "writer refuses to replace a destination");
    {
        std::ifstream     input(destination, std::ios::binary);
        const std::string content{ std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
        expect(content == kCanonicalXml, "destination-exists failure preserves original bytes");
    }
    {
        std::ifstream     input(sibling, std::ios::binary);
        const std::string content{ std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
        expect(content == "caller sentinel", "exclusive temp allocation preserves pre-existing sibling bytes");
    }
    expect(!std::filesystem::exists(scratch / "scenario-round-trip.xml.tmp-1"), "writer removes its exact temporary sibling");
    const auto invalid = WriteScenarioXml(ScenarioDocument{ { ScenarioEntry{}, ScenarioEntry{} } }, scratch / "invalid.xml");
    expect(!invalid && invalid.error().code == ScenarioError::DuplicateId && !std::filesystem::exists(scratch / "invalid.xml"), "invalid models create no destination");
    const auto missingParent = WriteScenarioXml(document, scratch / "missing-parent" / "scenario.xml");
    expect(!missingParent && missingParent.error().code == ScenarioError::OpenFailed, "missing parent reports temporary-open failure");
    const auto directory = WriteScenarioXml(document, scratch);
    expect(!directory && directory.error().code == ScenarioError::DestinationExists, "existing directory is also refused");
    std::filesystem::remove(destination, error);
    std::filesystem::remove(sibling, error);
}

} // namespace

int main(int argc, char** argv)
{
    testCanonicalRoundTrip();
    testXboxDefaultsAndAppend();
    testAllSignedVariators();
    testLocalesAndMapHandoff();
    testMapNameCollisions();
    testValidationReportsAllIssues();
    testUnknownAndExcludedInputs();
    testMalformedAndBoundedReader();
#ifdef _WIN32
    char*       scratchEnvironment     = nullptr;
    std::size_t scratchEnvironmentSize = 0;
    _dupenv_s(&scratchEnvironment, &scratchEnvironmentSize, "REREVVED_TOOLS_TEST_SCRATCH");
    const std::unique_ptr<char, decltype(&std::free)> scratchEnvironmentOwner(scratchEnvironment, &std::free);
#else
    const char* scratchEnvironment = std::getenv("REREVVED_TOOLS_TEST_SCRATCH");
#endif
    const auto      scratch       = argc > 1 ? std::filesystem::path(argv[1]) : scratchEnvironment ? std::filesystem::path(scratchEnvironment)
                                                                                                   : std::filesystem::current_path();
    const auto      testDirectory = scratch / "scenario-tests";
    std::error_code error;
    if (std::filesystem::create_directory(testDirectory, error))
    {
        testFileWriter(testDirectory);
        std::filesystem::remove(testDirectory, error);
        expect(!error, "file tests remove their empty directory");
    }
    else
    {
        expect(false, "file tests require a fresh scenario-tests directory under the supplied scratch parent");
    }
    return failures == 0 ? 0 : 1;
}
