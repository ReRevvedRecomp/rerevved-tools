#include "asset_pack_writer.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>

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
            path             = std::filesystem::temp_directory_path() /
                               ("rerevved-asset-pack-test-" + std::to_string(token) + "-" + std::to_string(sequence++));
            std::error_code error;
            if (std::filesystem::create_directory(path, error))
            {
                break;
            }
            if (error && error != std::errc::file_exists)
            {
                throw std::runtime_error("Could not create unique asset-pack test directory.");
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

AssetPackDescription example()
{
    AssetPackDescription description;
    description.id      = "example-pack";
    description.name    = "Example Pack";
    description.version = "1.0";
    description.assets  = {
        { "file-data/GFX_MainMenu_logo.dds", { std::byte{ 0 }, std::byte{ 0xFF }, std::byte{ 'D' } } },
        { "empty.bin", {} },
    };
    return description;
}

constexpr std::string_view kManifest =
    "manifest_version = 1\n"
    "\n"
    "[asset_pack]\n"
    "id = \"example-pack\"\n"
    "name = \"Example Pack\"\n"
    "version = \"1.0\"\n";

std::string read(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    expect(static_cast<bool>(input), "fixture read opens");
    return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
}

void write(const std::filesystem::path& path, std::string_view bytes)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    expect(!output.fail(), "fixture write finishes");
}

bool has(const std::vector<AssetPackViolation>& violations, AssetPackError error, bool warning = false)
{
    return std::ranges::any_of(violations, [&](const auto& violation)
                               {
                                   return violation.error == error && violation.warning == warning;
                               });
}

bool valid(const std::vector<AssetPackViolation>& violations)
{
    return std::ranges::none_of(violations, [](const auto& violation)
                                {
                                    return !violation.warning;
                                });
}

bool empty(const std::filesystem::path& path)
{
    return std::filesystem::directory_iterator(path) == std::filesystem::directory_iterator();
}

void testRoundTrip()
{
    TemporaryDirectory root;
    const auto         description = example();
    const auto         written     = WriteAssetPack(description, root.path);
    expect(written.has_value(), "valid pack writes");
    if (!written)
    {
        std::cerr << AssetPackErrorMessage(written.error()) << '\n';
        return;
    }
    const auto pack = root.path / description.id;
    expect(read(pack / "asset-pack.toml") == kManifest, "minimal manifest text is deterministic and exact");
    expect(ValidateAssetPack(pack).empty(), "round-trip has no violations or warnings");
    expect(ValidateAssetPack(pack / "").empty(), "a trailing separator preserves the pack directory identity");
    expect(ValidateAssetPack(pack / ".").empty() && ValidateAssetPack(pack / "assets" / "..").empty(),
           "dot spellings of the pack directory preserve its identity");
    for (const auto& asset : description.assets)
    {
        const auto bytes = read(pack / "assets" / asset.keyPath);
        expect(bytes.size() == asset.bytes.size() && std::equal(bytes.begin(), bytes.end(), asset.bytes.begin(), [](char left, std::byte right)
                                                                {
                                                                    return static_cast<unsigned char>(left) == std::to_integer<unsigned char>(right);
                                                                }),
               "round-trip retains every byte, including NUL, high bytes and empty files");
    }
    expect(std::distance(std::filesystem::directory_iterator(root.path), std::filesystem::directory_iterator()) == 1,
           "successful publication leaves only the ID directory");
}

void testEscaping()
{
    TemporaryDirectory root;
    auto               description = example();
    description.name               = "Quote \" and slash \\";
    description.author             = "";
    description.description        = std::string("\b\t\n\f\r") + '\0' + '\x1F' + '\x7F' + "\xC3\xA9\xF0\x9F\x98\x80";
    expect(WriteAssetPack(description, root.path).has_value(), "escaped metadata writes and validates");
    expect(read(root.path / description.id / "asset-pack.toml") ==
               "manifest_version = 1\n\n[asset_pack]\n"
               "id = \"example-pack\"\n"
               "name = \"Quote \\\" and slash \\\\\"\n"
               "version = \"1.0\"\n"
               "author = \"\"\n"
               "description = \"\\b\\t\\n\\f\\r\\u0000\\u001F\\u007F\\u00E9\\U0001F600\"\n",
           "manifest escaping is exact ASCII TOML, including Unicode scalar escapes");
}

void testDestinationExists()
{
    TemporaryDirectory root;
    const auto         description = example();
    const auto         pack        = root.path / description.id;
    write(pack / "asset-pack.toml", "keep manifest");
    write(pack / "assets" / "existing", "keep asset");
    const auto result = WriteAssetPack(description, root.path);
    expect(!result && result.error() == AssetPackError::DestinationExists, "existing directory is refused specifically");
    expect(read(pack / "asset-pack.toml") == "keep manifest" && read(pack / "assets" / "existing") == "keep asset",
           "existing destination bytes remain untouched");
    expect(!std::filesystem::exists(pack / "assets" / "empty.bin"), "existing destination receives no new assets");
    const auto fileRoot = root.path / "file-root";
    std::filesystem::create_directory(fileRoot);
    write(fileRoot / description.id, "keep file");
    const auto fileResult = WriteAssetPack(description, fileRoot);
    expect(!fileResult && fileResult.error() == AssetPackError::DestinationExists && read(fileRoot / description.id) == "keep file",
           "existing file is refused and untouched");
    const auto emptyRoot = root.path / "empty-root";
    std::filesystem::create_directories(emptyRoot / description.id);
    const auto emptyResult = WriteAssetPack(description, emptyRoot);
    expect(!emptyResult && emptyResult.error() == AssetPackError::DestinationExists && empty(emptyRoot / description.id),
           "empty destination directory is refused and untouched");
}

void testInvalidDestinationRoot()
{
    TemporaryDirectory root;
    const auto         missing = WriteAssetPack(example(), root.path / "missing");
    expect(!missing && missing.error() == AssetPackError::InvalidPackDirectory && empty(root.path),
           "missing output root is refused without creating directories");
    write(root.path / "file-root", "keep");
    const auto regularFile = WriteAssetPack(example(), root.path / "file-root");
    expect(!regularFile && regularFile.error() == AssetPackError::InvalidPackDirectory && read(root.path / "file-root") == "keep",
           "a regular file cannot be an output root and retains its bytes");
}

void testInvalidDescriptions()
{
    TemporaryDirectory root;
    const auto         rejected = [&](const AssetPackDescription& description, AssetPackError error)
    {
        const auto result = WriteAssetPack(description, root.path);
        expect(!result && result.error() == error, "invalid description returns its specific error");
        expect(empty(root.path), "invalid description creates no destination or staging directory");
    };
    for (const auto& id : { std::string("Bad_ID"), std::string(64, 'a'), std::string("a--b"), std::string(), std::string("-a"), std::string("a-"), std::string("a.b"), std::string("../a"), std::string("a b"), std::string("\xC3\xA9") })
    {
        auto description = example();
        description.id   = id;
        rejected(description, AssetPackError::InvalidId);
    }
    for (const auto version : { "", "1", "1.0.0", "1.0-dev", "-1.0", "+1.0", "\xD9\xA1.0", "1.", ".0", " 1.0", "1.0\n" })
    {
        auto description    = example();
        description.version = version;
        rejected(description, AssetPackError::InvalidVersion);
    }
    for (const auto name : { "", " \t\n", "\xC2\xA0", "\xE3\x80\x80" })
    {
        auto description = example();
        description.name = name;
        rejected(description, AssetPackError::InvalidName);
    }
    for (const auto key : { "", "/absolute", "C:/absolute", "C:relative", "//server/share", "a\\b", "..", "../outside", "a/../b", "a//b", "a/", "./a", "a/./b", "a:b" })
    {
        auto description              = example();
        description.assets[0].keyPath = key;
        rejected(description, AssetPackError::InvalidKeyPath);
    }
    auto description              = example();
    description.assets[0].keyPath = std::string("a\0b", 3);
    rejected(description, AssetPackError::InvalidKeyPath);
    description                 = example();
    description.manifestVersion = 2;
    rejected(description, AssetPackError::InvalidManifestVersion);
    description = example();
    description.assets.clear();
    rejected(description, AssetPackError::EmptyAssets);
    description = example();
    description.assets.push_back(description.assets.front());
    rejected(description, AssetPackError::DuplicateKeyPath);
    description        = example();
    description.assets = { { "parent", {} }, { "parent/child", {} } };
    rejected(description, AssetPackError::InvalidKeyPath);
    description        = example();
    description.author = "\xFF";
    rejected(description, AssetPackError::InvalidEncoding);
    description         = example();
    description.mapPack = MapPackInfo{ 5, { { 100, "\xFF" } } };
    rejected(description, AssetPackError::InvalidEncoding);
}

void testWindowsUnsafeKeys()
{
    TemporaryDirectory root;
    const auto         rejected = [&](std::string_view key, std::string_view category)
    {
        auto description              = example();
        description.assets[0].keyPath = key;
        const auto result             = WriteAssetPack(description, root.path);
        expect(!result && result.error() == AssetPackError::InvalidKeyPath,
               std::format("{} returns InvalidKeyPath", category));
        expect(empty(root.path), "Windows-unsafe key creates no destination or staging directory");
    };
    for (const auto key : { "x.", "x ", "x./file.bin", "x /file.bin", "parent/x.", "parent/x ", "parent/x./file.bin", "parent/x /file.bin" })
    {
        rejected(key, "trailing dot or space in a key component");
    }
    for (const auto character : std::string_view("<>\":|?*"))
    {
        const auto component = std::string("before") + character + "after";
        rejected(component, "forbidden Windows filename character");
        rejected("parent/" + component + "/file.bin", "forbidden Windows character in a nested component");
    }
    for (unsigned int value = 0; value < 0x20; ++value)
    {
        const auto component = std::string("before") + static_cast<char>(value) + "after";
        rejected(component, "ASCII control character");
        rejected("parent/" + component + "/file.bin", "ASCII control character in a nested component");
    }
    rejected("parent/before\x7F"
             "after/file.bin",
             "ASCII delete character");
    for (const auto device : { "CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$", "COM0", "LPT0", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9" })
    {
        std::string name(device);
        rejected(name, "reserved Windows device name");
        rejected(name + ".txt", "reserved Windows device name with an extension");
        rejected(name + " .txt", "reserved Windows device name with spaces before an extension");
        for (auto& character : name)
        {
            if (character >= 'A' && character <= 'Z')
            {
                character = static_cast<char>(character - 'A' + 'a');
            }
        }
        rejected("parent/" + name, "lowercase nested Windows device name");
        name[0] = device[0];
        rejected(name + "/file.bin", "mixed-case Windows device directory");
        rejected("parent/" + name + ".txt/file.bin", "mixed-case nested device directory with an extension");
        rejected("parent/" + name + ".txt.more", "mixed-case nested device file with multiple extensions");
    }
}

void testWindowsSafeKeys()
{
    TemporaryDirectory root;
    auto               description = example();
    description.assets.clear();
    for (const auto key : { "CONSOLE", "PRN-data", "AUXiliary", "NULish", "COM10", "LPT10", "file.NUL", "parent.with.dots/with space.bin", "parent/\xC3\xA9.bin" })
    {
        description.assets.push_back({ key, { std::byte{ 1 } } });
    }
    const auto result = WriteAssetPack(description, root.path);
    expect(result.has_value(), "device-like names, internal dots/spaces and UTF-8 keys write successfully");
    if (result)
    {
        expect(ValidateAssetPack(root.path / description.id).empty(), "Windows-safe boundary keys validate after publication");
    }
}

void testAcceptedBoundaries()
{
    TemporaryDirectory root;
    auto               description = example();
    description.id                 = std::string(63, 'a');
    description.version            = "000000000000000000000000000000000000000001.0000";
    description.description        = "";
    expect(WriteAssetPack(description, root.path).has_value(), "63-character ID, leading zeroes and unbounded numeric version components are valid");
    description.id      = "0";
    description.version = "0.0";
    description.name    = std::string(1024, 'x');
    expect(WriteAssetPack(description, root.path).has_value(), "numeric ID, zero version and long name are valid");
}

void testCaseDistinctKeys()
{
    TemporaryDirectory root;
    auto               description = example();
    description.assets             = { { "Case.bin", { std::byte{ 1 } } }, { "case.bin", { std::byte{ 2 } } } };
    const auto result              = WriteAssetPack(description, root.path);
    if (result)
    {
        expect(read(root.path / description.id / "assets" / "Case.bin") == std::string(1, '\1') &&
                   read(root.path / description.id / "assets" / "case.bin") == std::string(1, '\2'),
               "case-sensitive filesystem preserves both keys as allowed by verifier");
    }
    else
    {
        expect(result.error() == AssetPackError::OpenFailed, "case-insensitive filesystem reports create refusal rather than a schema duplicate");
        expect(empty(root.path), "case collision leaves no staging or published directory");
    }
}

void testMidWriteCleanup()
{
    TemporaryDirectory root;
    write(root.path / "unrelated.txt", "untouched");
    auto description = example();
    // The first file is writable, but the next basename exceeds Windows and Linux filesystem limits.
    description.assets = { { "a-first.bin", { std::byte{ 1 } } }, { "z/" + std::string(300, 'x'), {} } };
    const auto result  = WriteAssetPack(description, root.path);
    expect(!result && result.error() == AssetPackError::OpenFailed, "mid-write filename failure returns OpenFailed");
    expect(!std::filesystem::exists(root.path / description.id), "mid-write failure publishes no pack");
    expect(std::distance(std::filesystem::directory_iterator(root.path), std::filesystem::directory_iterator()) == 1 &&
               read(root.path / "unrelated.txt") == "untouched",
           "mid-write failure removes only its staging directory");
}

void testManifestViolations()
{
    TemporaryDirectory root;
    const auto         pack = root.path / "example-pack";
    write(pack / "assets" / "synthetic", "payload");
    const auto check = [&](std::string_view text, AssetPackError error)
    {
        write(pack / "asset-pack.toml", text);
        expect(has(ValidateAssetPack(pack), error),
               std::format("hand-made manifest expected violation: {}\ncase:\n{}", AssetPackErrorMessage(error), text));
    };
    for (const auto value : { "0", "2", "true", "1.0", "\"1\"" })
    {
        check(std::string("manifest_version = ") + value + "\n[asset_pack]\nid = \"example-pack\"\nname = \"Name\"\nversion = \"1.0\"\n",
              AssetPackError::InvalidManifestVersion);
    }
    check("[asset_pack]\nid = \"example-pack\"\nname = \"Name\"\nversion = \"1.0\"\n", AssetPackError::InvalidManifestVersion);
    check("manifest_version = 1\nasset_pack = true\n", AssetPackError::MissingPackTable);
    check("manifest_version = 1\n", AssetPackError::MissingPackTable);
    for (const auto field : { "id", "name", "version" })
    {
        std::string text(kManifest);
        const auto  start = text.find("\n" + std::string(field) + " =") + 1;
        text.erase(start, text.find('\n', start) - start + 1);
        check(text, AssetPackError::MissingField);
    }
    for (const auto& [field, value, error] : {
             std::tuple{ "id", "\"Bad_ID\"", AssetPackError::InvalidId },
             std::tuple{ "id", "1", AssetPackError::InvalidId },
             std::tuple{ "id", "\"other-pack\"", AssetPackError::IdMismatch },
             std::tuple{ "name", "\" \"", AssetPackError::InvalidName },
             std::tuple{ "name", "1", AssetPackError::InvalidName },
             std::tuple{ "version", "\"1.0.0\"", AssetPackError::InvalidVersion },
             std::tuple{ "version", "1", AssetPackError::InvalidVersion } })
    {
        std::string text(kManifest);
        const auto  start = text.find("\n" + std::string(field) + " =") + 1;
        text.replace(start, text.find('\n', start) - start, std::string(field) + " = " + value);
        check(text, error);
    }
    check(std::string(kManifest) + "author = 1\n", AssetPackError::InvalidAuthor);
    check(std::string(kManifest) + "description = false\n", AssetPackError::InvalidDescription);
    check(std::string(kManifest) + "description = \"\xFF\"\n", AssetPackError::InvalidEncoding);
    check(std::string(kManifest) + "name = \"again\"\n", AssetPackError::InvalidManifest);
    check(std::string(kManifest) + "description = \"\\uD800\"\n", AssetPackError::UnsupportedManifestSyntax);
    check("[", AssetPackError::UnsupportedManifestSyntax);
    write(pack / "asset-pack.toml", std::string("unknown = true\n") + std::string(kManifest) + "unknown = \"metadata\"\n");
    const auto warnings = ValidateAssetPack(pack);
    expect(valid(warnings) && has(warnings, AssetPackError::UnknownField, true), "unknown top-level and pack fields warn without invalidating a pack");
    write(pack / "asset-pack.toml", "manifest_version = +1 # comment\n\n[asset_pack] # comment\nid = 'example-pack'\nname = 'Name # literal'\nversion = '1.0'\n");
    expect(ValidateAssetPack(pack).empty(), "literal strings, comments, and integer +1 validate");
    const std::string mapPack = std::string(kManifest) + "\n[map_pack]\nslot = 7\n\n[[map_pack.scenarios]]\nid = 105\nname = \"Isle\"\n\n[[map_pack.scenarios]]\nid = 106\nname = \"Coast\"\n";
    write(pack / "asset-pack.toml", mapPack);
    expect(ValidateAssetPack(pack).empty(), "a [map_pack] table with scenario tables validates");
    check(std::string(kManifest) + "\n[map_pack]\nslot = \"7\"\n", AssetPackError::InvalidManifest);
    check(std::string(kManifest) + "\n[map_pack]\nslot = 9223372036854775808\n", AssetPackError::InvalidManifest);
    check(std::string(kManifest) + "\n[[map_pack.scenarios]]\nid = 1.5\nname = \"Isle\"\n", AssetPackError::InvalidManifest);
    check(std::string(kManifest) + "\n[[map_pack.scenarios]]\nid = 105\nname = 1\n", AssetPackError::InvalidManifest);
    check(std::string(kManifest) + "\n[[other]]\nid = 1\n", AssetPackError::UnsupportedManifestSyntax);
}

void testTreeViolations()
{
    TemporaryDirectory root;
    expect(has(ValidateAssetPack(root.path / "absent"), AssetPackError::InvalidPackDirectory), "missing pack directory is reported");
    const auto pack = root.path / "example-pack";
    std::filesystem::create_directory(pack);
    auto violations = ValidateAssetPack(pack);
    expect(has(violations, AssetPackError::MissingManifest) && has(violations, AssetPackError::InvalidAssetsDirectory) &&
               has(violations, AssetPackError::EmptyAssets),
           "validation collects all independent missing-tree errors");
    write(pack / "asset-pack.toml", kManifest);
    std::filesystem::create_directories(pack / "assets" / "empty-directory");
    expect(has(ValidateAssetPack(pack), AssetPackError::EmptyAssets), "directory-only assets tree is empty");
    write(pack / "assets" / "zero-bytes", "");
    expect(ValidateAssetPack(pack).empty(), "one zero-byte asset satisfies nonempty tree rule");
    write(pack / "icon.png", "synthetic icon");
    write(pack / "README.md", "synthetic readme");
    write(pack / "LICENSE.txt", "synthetic license");
    std::filesystem::create_directory(pack / "unused-empty-directory");
    expect(ValidateAssetPack(pack).empty(), "allowed metadata files and empty directories validate");
    write(pack / "main.lua", "unsupported");
    write(pack / "other" / "file.bin", "unsupported");
    violations = ValidateAssetPack(pack);
    expect(std::ranges::count_if(violations, [](const auto& violation)
                                 {
                                     return violation.error == AssetPackError::UnsupportedFile;
                                 }) == 2,
           "every unsupported root and nested file is reported");
    const auto badId = root.path / "Bad_ID";
    write(badId / "asset-pack.toml", kManifest);
    write(badId / "assets" / "file", "payload");
    expect(has(ValidateAssetPack(badId), AssetPackError::InvalidId) && has(ValidateAssetPack(badId), AssetPackError::IdMismatch),
           "invalid directory ID and manifest directory mismatch are reported together");
    const auto badAssets = root.path / "bad-assets";
    write(badAssets / "asset-pack.toml", kManifest);
    write(badAssets / "assets", "file instead of directory");
    expect(has(ValidateAssetPack(badAssets), AssetPackError::InvalidAssetsDirectory), "assets file is not a directory");
}

void testLinks()
{
    TemporaryDirectory root;
    const auto         pack = root.path / "example-pack";
    write(pack / "asset-pack.toml", kManifest);
    write(pack / "assets" / "file", "payload");
    write(root.path / "outside", "untouched");
    std::error_code error;
    const auto      link = pack / "assets" / "link";
    std::filesystem::create_symlink(root.path / "outside", link, error);
    if (!error)
    {
        expect(has(ValidateAssetPack(pack), AssetPackError::ReparsePoint), "asset symlink is rejected without reading its target");
        std::filesystem::remove(link);
    }
    else
    {
        std::cout << "SKIP: symlink fixture unavailable: " << error.message() << '\n';
    }
    error.clear();
    const auto linkedRoot = root.path / "linked-root";
    std::filesystem::create_directory_symlink(pack, linkedRoot, error);
    if (!error)
    {
        expect(has(ValidateAssetPack(linkedRoot), AssetPackError::ReparsePoint), "pack directory symlink is rejected");
        const auto result = WriteAssetPack(example(), linkedRoot);
        expect(!result && result.error() == AssetPackError::ReparsePoint, "writer rejects a linked destination root");
        std::filesystem::remove(linkedRoot);
    }
    expect(read(root.path / "outside") == "untouched", "link validation preserves outside bytes");
}

void testErrorMessages()
{
    expect(AssetPackErrorMessage(AssetPackError::InvalidVersion) == "The asset pack version must use numeric major.minor syntax.",
           "version diagnostic is stable");
    expect(AssetPackErrorMessage(AssetPackError::DestinationExists) == "The asset pack destination already exists.",
           "destination diagnostic is stable");
    for (int value = static_cast<int>(AssetPackError::InvalidManifestVersion); value <= static_cast<int>(AssetPackError::CleanupFailed); ++value)
    {
        expect(AssetPackErrorMessage(static_cast<AssetPackError>(value)) != "Unknown asset pack error.", "every declared error has an owning message");
    }
}

} // namespace

int main()
{
    testRoundTrip();
    testEscaping();
    testDestinationExists();
    testInvalidDestinationRoot();
    testInvalidDescriptions();
    testWindowsUnsafeKeys();
    testWindowsSafeKeys();
    testAcceptedBoundaries();
    testCaseDistinctKeys();
    testMidWriteCleanup();
    testManifestViolations();
    testTreeViolations();
    testLinks();
    testErrorMessages();
    if (failures != 0)
    {
        std::cerr << failures << " test(s) failed.\n";
    }
    return failures == 0 ? 0 : 1;
}
