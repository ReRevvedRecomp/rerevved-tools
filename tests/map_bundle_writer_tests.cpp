#include "map_bundle_writer.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

using namespace rerevved::tools;

namespace
{

int failures = 0;

void expect(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::vector<std::byte> bytes(std::string_view text)
{
    const auto span = std::as_bytes(std::span(text.data(), text.size()));
    return { span.begin(), span.end() };
}

std::vector<std::byte> readFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    expect(static_cast<bool>(input), "open the synthetic file");
    if (!input)
    {
        return {};
    }
    const auto size = input.tellg();
    expect(size >= 0, "measure the synthetic file");
    if (size < 0)
    {
        return {};
    }
    std::vector<std::byte> result(static_cast<std::size_t>(size));
    input.seekg(0);
    if (!result.empty())
    {
        input.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(result.size()));
    }
    expect(static_cast<bool>(input), "read every synthetic file byte");
    return result;
}

void writeFile(const std::filesystem::path& path, std::string_view text)
{
    std::ofstream output(path, std::ios::binary | std::ios::noreplace);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.close();
    expect(static_cast<bool>(output), "create only a new synthetic file");
}

void removeOwned(const std::filesystem::path& path)
{
    std::error_code error;
    expect(std::filesystem::remove(path, error) && !error, "remove exactly an owned file or empty directory");
}

std::set<std::filesystem::path> children(const std::filesystem::path& root)
{
    std::set<std::filesystem::path>           result;
    std::error_code                           error;
    std::filesystem::directory_iterator       iterator(root, error);
    const std::filesystem::directory_iterator end;
    while (!error && iterator != end)
    {
        result.insert(iterator->path().filename());
        iterator.increment(error);
    }
    expect(!error, "inspect only the synthetic parent directory");
    return result;
}

MapBundleArtifact artifact(std::string name, std::string_view payload)
{
    return { std::move(name), bytes(payload), [](std::span<const std::byte>) -> std::expected<void, std::string>
             {
                 return {};
             } };
}

void testSha256()
{
    expect(CalculateMapBundleSha256({}) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "SHA-256 empty-input known answer");
    expect(CalculateMapBundleSha256(bytes("abc")) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 abc known answer");
    expect(CalculateMapBundleSha256(bytes("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", "SHA-256 56-byte known answer requires a second padding block");
    expect(CalculateMapBundleSha256(bytes(std::string(64, 'a'))) == "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb", "SHA-256 complete-block known answer");
}

void testSuccessAndManifest(const std::filesystem::path& root)
{
    const std::string        longPayload = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    std::array               artifacts{ artifact("empty.bin", ""), artifact("map.bin", "abc"), artifact("scenario.bin", longPayload) };
    const auto               before = children(root);
    std::vector<std::string> verified;
    for (auto& item : artifacts)
    {
        const auto* original = &item;
        item.verify          = [&, original](std::span<const std::byte> read) -> std::expected<void, std::string>
        {
            expect(std::ranges::equal(read, original->bytes), "verifier receives byte-exact read-back");
            if (!read.empty())
            {
                expect(read.data() != original->bytes.data(), "verifier receives a disk snapshot rather than the input vector");
            }
            const auto                         during = children(root);
            std::vector<std::filesystem::path> added;
            std::ranges::set_difference(during, before, std::back_inserter(added));
            expect(added.size() == 1, "only one fresh staging directory exists before publication");
            if (added.size() == 1)
            {
                expect(readFile(root / added.front() / original->name) == original->bytes, "artifact exists on disk before its verifier runs");
                expect(!std::filesystem::exists(root / added.front() / kMapBundleManifestName), "manifest is absent while artifact verifiers run");
            }
            expect(!std::filesystem::exists(root / "success"), "final directory is absent while staging is verified");
            verified.push_back(original->name);
            return {};
        };
    }
    const auto result = WriteMapBundle(root, "success", artifacts, "test-1.0");
    expect(result && *result == root / "success", "publish the complete directory at the requested destination");
    expect(verified == std::vector<std::string>{ "empty.bin", "map.bin", "scenario.bin" }, "verify artifacts in caller order");
    auto expectedChildren = before;
    expectedChildren.insert("success");
    expect(children(root) == expectedChildren, "successful publication leaves no staging directory");
    const std::string expectedManifest =
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"label\": \"intermediate - not playable\",\n"
        "  \"toolVersion\": \"test-1.0\",\n"
        "  \"artifacts\": [\n"
        "    {\"name\": \"empty.bin\", \"size\": 0, \"sha256\": \"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\"},\n"
        "    {\"name\": \"map.bin\", \"size\": 3, \"sha256\": \"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\"},\n"
        "    {\"name\": \"scenario.bin\", \"size\": 56, \"sha256\": \"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1\"}\n"
        "  ]\n"
        "}\n";
    expect(readFile(root / "success" / kMapBundleManifestName) == bytes(expectedManifest), "manifest bytes exactly match the hand-written format and independent digests");
    for (const auto& item : artifacts)
    {
        expect(readFile(root / "success" / item.name) == item.bytes, "published artifact remains byte-exact");
    }
    expect(children(root / "success") == std::set<std::filesystem::path>{ "empty.bin", "map.bin", "scenario.bin", std::filesystem::path(kMapBundleManifestName) }, "published directory contains exactly the artifacts and manifest");
    const auto refused = WriteMapBundle(root, "success", artifacts, "other-version");
    expect(!refused && refused.error() == MapBundleWriteError::DestinationExists, "reject an existing populated destination");
    expect(verified.size() == 3 && readFile(root / "success" / kMapBundleManifestName) == bytes(expectedManifest), "rejection invokes no verifier and leaves existing output unchanged");
    for (const auto& item : artifacts)
    {
        removeOwned(root / "success" / item.name);
    }
    removeOwned(root / "success" / kMapBundleManifestName);
    removeOwned(root / "success");
}

void testPreflight(const std::filesystem::path& root)
{
    const auto before = children(root);
    std::array artifacts{ artifact("same.bin", "abc"), artifact("same.bin", "other") };
    auto       result = WriteMapBundle(root, "rejected", artifacts, "1");
    expect(!result && result.error() == MapBundleWriteError::DuplicateName, "reject exact duplicate names");
    artifacts[1].name = "SAME.BIN";
    result            = WriteMapBundle(root, "rejected", artifacts, "1");
    expect(!result && result.error() == MapBundleWriteError::DuplicateName, "reject ASCII case-folded duplicates on every platform");
    artifacts[1].name = "BUNDLE-MANIFEST.JSON";
    result            = WriteMapBundle(root, "rejected", artifacts, "1");
    expect(!result && result.error() == MapBundleWriteError::DuplicateName, "reserve the manifest name after case folding");
    for (const std::string name : { "", "..", "../escape", "sub/file", "sub\\file", "name:stream", "CON.txt", "lpt1", "trailing.", "trailing ", "\x7f", "\x80" })
    {
        artifacts[1].name = name;
        result            = WriteMapBundle(root, "rejected", artifacts, "1");
        expect(!result && result.error() == MapBundleWriteError::InvalidName, "reject unsafe Windows filesystem artifact basenames");
    }
    artifacts[1]        = artifact("other.bin", "xyz");
    artifacts[1].verify = {};
    result              = WriteMapBundle(root, "rejected", artifacts, "1");
    expect(!result && result.error() == MapBundleWriteError::MissingVerifier, "require an artifact verifier before mutation");
    artifacts[1] = artifact("other.bin", "xyz");
    result       = WriteMapBundle(root, "../rejected", artifacts, "1");
    expect(!result && result.error() == MapBundleWriteError::InvalidName, "reject a path-bearing bundle directory name");
    result = WriteMapBundle(root, "rejected", artifacts, std::string_view("\xC0\x80", 2));
    expect(!result && result.error() == MapBundleWriteError::InvalidToolVersion, "reject malformed UTF-8 in the manifest tool version");
    expect(children(root) == before, "invalid input creates no staging or destination entries");
    result = WriteMapBundle(root / "missing", "rejected", artifacts, "1");
    expect(!result && result.error() == MapBundleWriteError::InvalidParent && children(root) == before,
           "missing bundle parent is rejected without creating it");
    for (const auto name : { std::string(256, 'a'), std::string("bad\0name", 8), std::string("name?"), std::string("name<") })
    {
        artifacts[1].name = name;
        result            = WriteMapBundle(root, "rejected", artifacts, "1");
        expect(!result && result.error() == MapBundleWriteError::InvalidName && children(root) == before,
               "overlong, embedded NUL and forbidden-character names are rejected before staging");
    }
    artifacts[1] = artifact("other.bin", "xyz");
    expect(std::filesystem::create_directory(root / "occupied-empty"), "create an existing empty destination");
    result = WriteMapBundle(root, "occupied-empty", artifacts, "1");
    expect(!result && result.error() == MapBundleWriteError::DestinationExists && std::filesystem::is_empty(root / "occupied-empty"), "refuse even an empty destination directory");
    removeOwned(root / "occupied-empty");
    writeFile(root / "occupied-file", "keep");
    result = WriteMapBundle(root, "occupied-file", artifacts, "1");
    expect(!result && result.error() == MapBundleWriteError::DestinationExists && readFile(root / "occupied-file") == bytes("keep"), "refuse a destination file without modification");
    removeOwned(root / "occupied-file");
}

void testVerifierFailure(const std::filesystem::path& root)
{
    writeFile(root / "parent-sentinel", "parent bytes");
    expect(std::filesystem::create_directory(root / ".existing-staging"), "create an unrelated staging-like directory");
    writeFile(root / ".existing-staging" / "sentinel", "unrelated bytes");
    const auto before = children(root);
    std::array artifacts{ artifact("first.bin", "first"), artifact("bad.bin", "second"), artifact("last.bin", "third") };
    int        called   = 0;
    artifacts[1].verify = [&](std::span<const std::byte>) -> std::expected<void, std::string>
    {
        ++called;
        return std::unexpected("synthetic rejection");
    };
    artifacts[2].verify = [&](std::span<const std::byte>) -> std::expected<void, std::string>
    {
        ++called;
        return {};
    };
    auto result = WriteMapBundle(root, "failed", artifacts, "1");
    expect(!result && result.error() == MapBundleWriteError::VerificationFailed && called == 1, "stop at the first failed verifier with its typed error");
    expect(children(root) == before && !std::filesystem::exists(root / "failed"), "failure removes precisely the fresh staging directory");
    expect(readFile(root / "parent-sentinel") == bytes("parent bytes") && readFile(root / ".existing-staging" / "sentinel") == bytes("unrelated bytes"), "failure leaves parent and unrelated staging-like bytes unchanged");
    artifacts[1].verify = [](std::span<const std::byte>) -> std::expected<void, std::string>
    {
        throw std::runtime_error("synthetic verifier exception");
    };
    result = WriteMapBundle(root, "failed", artifacts, "1");
    expect(!result && result.error() == MapBundleWriteError::VerificationFailed && children(root) == before, "a throwing verifier also cleans only owned staging");
    removeOwned(root / "parent-sentinel");
    removeOwned(root / ".existing-staging" / "sentinel");
    removeOwned(root / ".existing-staging");
}

void testDestinationAppears(const std::filesystem::path& root)
{
    std::array artifacts{ artifact("payload.bin", "abc") };
    artifacts[0].verify = [&](std::span<const std::byte>) -> std::expected<void, std::string>
    {
        expect(std::filesystem::create_directory(root / "late-destination"), "simulate a destination appearing after staging creation");
        return {};
    };
    const auto result = WriteMapBundle(root, "late-destination", artifacts, "1");
    expect(!result && result.error() == MapBundleWriteError::DestinationExists, "publication refuses a destination that appeared during verification");
    expect(children(root) == std::set<std::filesystem::path>{ "late-destination" } && std::filesystem::is_empty(root / "late-destination"), "failed publication removes only staging and preserves the appearing empty destination");
    removeOwned(root / "late-destination");
}

void testSessionOwnership(const std::filesystem::path& root)
{
    const auto           staging = root / "owned-stage";
    MapBundleFileSession session;
    expect(session.CreateDirectoryNew(staging).has_value(), "production seam creates an exclusive owned directory");
    expect(session.WriteNew(staging / "owned.bin", bytes("abc")).has_value(), "production seam writes a new owned file");
    expect(!session.WriteNew(root / "outside.bin", bytes("x")), "production seam rejects a write outside staging");
    const auto duplicate = session.WriteNew(staging / "OWNED.BIN", bytes("x"));
    expect(!duplicate && duplicate.error() == MapBundleWriteError::DuplicateName, "production seam also refuses folded duplicate file names");
    const auto read = session.Read(staging / "owned.bin");
    expect(read && *read == bytes("abc"), "production read seam returns every disk byte");
    const auto wrongRead = session.Read(root / "outside.bin");
    expect(!wrongRead && wrongRead.error() == MapBundleWriteError::InvalidStaging, "read seam refuses a path not owned by this session");
    const auto wrongCleanup = session.RemoveOwnedStaging(root);
    expect(!wrongCleanup && wrongCleanup.error() == MapBundleWriteError::CleanupFailed && readFile(staging / "owned.bin") == bytes("abc"),
           "cleanup refuses the caller's parent and preserves staged bytes");
    writeFile(staging / "unowned.bin", "foreign");
    expect(std::filesystem::create_directory(staging / "unowned-dir"), "create an unowned nested directory");
    writeFile(staging / "unowned-dir" / "sentinel", "nested foreign bytes");
    const auto published = session.PublishDirectoryNew(staging, root / "foreign-rejected");
    expect(!published && !std::filesystem::exists(root / "foreign-rejected"), "publication refuses an unowned extra entry");
    const auto cleanup = session.RemoveOwnedStaging(staging);
    expect(!cleanup && cleanup.error() == MapBundleWriteError::CleanupFailed, "cleanup refuses to remove an unowned entry");
    expect(!std::filesystem::exists(staging / "owned.bin") && readFile(staging / "unowned.bin") == bytes("foreign"), "cleanup removes only the recorded owned file");
    expect(readFile(staging / "unowned-dir" / "sentinel") == bytes("nested foreign bytes"), "cleanup never recurses into an unowned directory");
    removeOwned(staging / "unowned.bin");
    removeOwned(staging / "unowned-dir" / "sentinel");
    removeOwned(staging / "unowned-dir");
    expect(session.RemoveOwnedStaging(staging).has_value(), "cleanup can finish after the fixture removes its own foreign entry");
    {
        MapBundleFileSession abandoned;
        expect(abandoned.CreateDirectoryNew(staging).has_value(), "create an abandoned owned stage");
        expect(abandoned.WriteNew(staging / "owned.bin", bytes("abc")).has_value(), "write the abandoned stage's owned file");
    }
    expect(!std::filesystem::exists(staging), "session destruction cleans its owned staging directory");
}

void testManifestEscaping()
{
    const auto encoded = EncodeMapBundleManifest({}, "v\"\\\n");
    expect(encoded && *encoded == bytes("{\n  \"schemaVersion\": 1,\n  \"label\": \"intermediate - not playable\",\n  \"toolVersion\": \"v\\\"\\\\\\u000a\",\n  \"artifacts\": []\n}\n"), "manifest escapes caller version text deterministically without a BOM");
    expect(MapBundleWriteErrorMessage(MapBundleWriteError::DestinationExists) == "The bundle destination already exists.", "destination error has its owning message");
    expect(MapBundleWriteErrorMessage(MapBundleWriteError::VerificationFailed) == "A bundle artifact readback verifier failed.", "verifier error has its owning message");
}

} // namespace

int main(int argc, char** argv)
{
    const auto      scratch = argc == 3 && std::string_view(argv[1]) == "--scratch-root" ? std::filesystem::path(argv[2]) : std::filesystem::temp_directory_path();
    const auto      root    = scratch / ("rerevved-tools-map-bundle-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code error;
    const bool      created = std::filesystem::create_directory(root, error);
    expect(created && !error, "create a fresh synthetic map bundle test directory");
    if (created && !error)
    {
        testSha256();
        testSuccessAndManifest(root);
        testPreflight(root);
        testVerifierFailure(root);
        testDestinationAppears(root);
        testSessionOwnership(root);
        testManifestEscaping();
        removeOwned(root);
    }
    if (failures != 0)
    {
        std::cerr << failures << " map bundle writer test(s) failed\n";
    }
    return failures == 0 ? 0 : 1;
}
