#include "map_bundle_writer.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <limits>
#include <set>
#include <system_error>

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

std::string folded(std::string_view name)
{
    std::string result(name);
    for (auto& character : result)
    {
        if (character >= 'A' && character <= 'Z')
        {
            character = static_cast<char>(character + ('a' - 'A'));
        }
    }
    return result;
}

bool validName(std::string_view name)
{
    if (name.empty() || name.size() > 255 || name.back() == '.' || name.back() == ' ')
    {
        return false;
    }
    for (const unsigned char character : name)
    {
        if (character < 0x20 || character >= 0x7F || std::string_view("<>:\"/\\|?*").find(static_cast<char>(character)) != std::string_view::npos)
        {
            return false;
        }
    }
    const auto lower = folded(name);
    auto       stem  = std::string_view(lower).substr(0, lower.find('.'));
    // Windows ignores trailing spaces before the extension when matching device names.
    while (stem.ends_with(' '))
    {
        stem.remove_suffix(1);
    }
    if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" || stem == "conin$" || stem == "conout$")
    {
        return false;
    }
    return !(stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) && stem[3] >= '1' && stem[3] <= '9');
}

bool validUtf8(std::string_view text)
{
    for (std::size_t i = 0; i < text.size();)
    {
        const auto first = static_cast<unsigned char>(text[i++]);
        if (first < 0x80)
        {
            continue;
        }
        const unsigned count = first >= 0xC2 && first <= 0xDF ? 1 : first >= 0xE0 && first <= 0xEF ? 2
                                                                : first >= 0xF0 && first <= 0xF4   ? 3
                                                                                                   : 0;
        if (count == 0 || text.size() - i < count)
        {
            return false;
        }
        const auto second = static_cast<unsigned char>(text[i]);
        if ((first == 0xE0 && second < 0xA0) || (first == 0xED && second >= 0xA0) ||
            (first == 0xF0 && second < 0x90) || (first == 0xF4 && second >= 0x90))
        {
            return false;
        }
        for (unsigned j = 0; j < count; ++j)
        {
            const auto next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xC0) != 0x80)
            {
                return false;
            }
        }
    }
    return true;
}

std::string jsonString(std::string_view value)
{
    constexpr std::string_view hex    = "0123456789abcdef";
    std::string                result = "\"";
    for (const unsigned char character : value)
    {
        if (character == '"' || character == '\\')
        {
            result += '\\';
            result += static_cast<char>(character);
        }
        else if (character < 0x20)
        {
            result += "\\u00";
            result += hex[character >> 4];
            result += hex[character & 15];
        }
        else
        {
            result += static_cast<char>(character);
        }
    }
    return result + '"';
}

std::expected<void, MapBundleWriteError> validateArtifacts(std::span<const MapBundleArtifact> artifacts)
{
    std::set<std::string> names{ folded(kMapBundleManifestName) };
    for (const auto& artifact : artifacts)
    {
        if (!validName(artifact.name))
        {
            return std::unexpected(MapBundleWriteError::InvalidName);
        }
        if (!names.insert(folded(artifact.name)).second)
        {
            return std::unexpected(MapBundleWriteError::DuplicateName);
        }
    }
    return {};
}

// Inspect the original route before normalization, including any link/.. pair.
std::expected<std::filesystem::path, MapBundleWriteError> safePath(const std::filesystem::path& path)
{
    if (path.empty() || path.native().find(std::filesystem::path::value_type{}) != std::filesystem::path::string_type::npos ||
        (path.has_root_name() && !path.has_root_directory()))
    {
        return std::unexpected(MapBundleWriteError::InvalidParent);
    }
    std::error_code error;
    const auto      absolute = std::filesystem::absolute(path, error);
    if (error)
    {
        return std::unexpected(MapBundleWriteError::InvalidParent);
    }
    auto       current = absolute.root_path();
    const auto inspect = [](const std::filesystem::path& entry) -> std::expected<void, MapBundleWriteError>
    {
#ifdef _WIN32
        const auto attributes = GetFileAttributesW(entry.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        {
            return std::unexpected(MapBundleWriteError::ReparsePoint);
        }
#endif
        std::error_code statusError;
        const auto      status = std::filesystem::symlink_status(entry, statusError);
        if (statusError && statusError != std::errc::no_such_file_or_directory)
        {
            return std::unexpected(MapBundleWriteError::ReadFailed);
        }
        if (std::filesystem::is_symlink(status))
        {
            return std::unexpected(MapBundleWriteError::ReparsePoint);
        }
        return {};
    };
    if (const auto checked = inspect(current); !checked)
    {
        return std::unexpected(checked.error());
    }
    for (const auto& part : absolute.relative_path())
    {
        current /= part;
        if (const auto checked = inspect(current); !checked)
        {
            return std::unexpected(checked.error());
        }
    }
    return absolute.lexically_normal();
}

std::expected<void, MapBundleWriteError> requireMissing(const std::filesystem::path& path)
{
    std::error_code error;
    const auto      status = std::filesystem::symlink_status(path, error);
    if (error && error != std::errc::no_such_file_or_directory)
    {
        return std::unexpected(MapBundleWriteError::ReadFailed);
    }
    if (status.type() != std::filesystem::file_type::not_found)
    {
        return std::unexpected(MapBundleWriteError::DestinationExists);
    }
    return {};
}

std::expected<void, MapBundleWriteError> renameNew(const std::filesystem::path& source, const std::filesystem::path& destination)
{
#ifdef _WIN32
    if (MoveFileExW(source.c_str(), destination.c_str(), 0) != 0)
    {
        return {};
    }
    const auto error = GetLastError();
    return std::unexpected(error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS || !requireMissing(destination)
                               ? MapBundleWriteError::DestinationExists
                               : MapBundleWriteError::PublishFailed);
#elif defined(__linux__)
    if (syscall(SYS_renameat2, AT_FDCWD, source.c_str(), AT_FDCWD, destination.c_str(), 1U) == 0)
    {
        return {};
    }
    return std::unexpected(errno == EEXIST ? MapBundleWriteError::DestinationExists : MapBundleWriteError::PublishFailed);
#else
    static_cast<void>(source);
    static_cast<void>(destination);
    return std::unexpected(MapBundleWriteError::PublishFailed);
#endif
}

} // namespace

bool IsValidMapBundleName(std::string_view name)
{
    return validName(name);
}

std::string CalculateMapBundleSha256(std::span<const std::byte> bytes)
{
    constexpr std::array<std::uint32_t, 64> constants{
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
    };
    std::array<std::uint32_t, 8> state{ 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    const auto                   compress = [&](std::span<const std::byte, 64> block)
    {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t i = 0; i < 16; ++i)
        {
            for (std::size_t j = 0; j < 4; ++j)
            {
                words[i] = (words[i] << 8) | std::to_integer<std::uint32_t>(block[i * 4 + j]);
            }
        }
        for (std::size_t i = 16; i < words.size(); ++i)
        {
            const auto s0 = std::rotr(words[i - 15], 7) ^ std::rotr(words[i - 15], 18) ^ (words[i - 15] >> 3);
            const auto s1 = std::rotr(words[i - 2], 17) ^ std::rotr(words[i - 2], 19) ^ (words[i - 2] >> 10);
            words[i]      = words[i - 16] + s0 + words[i - 7] + s1;
        }
        auto working = state;
        for (std::size_t i = 0; i < words.size(); ++i)
        {
            const auto sum1     = std::rotr(working[4], 6) ^ std::rotr(working[4], 11) ^ std::rotr(working[4], 25);
            const auto choice   = (working[4] & working[5]) ^ (~working[4] & working[6]);
            const auto t1       = working[7] + sum1 + choice + constants[i] + words[i];
            const auto sum0     = std::rotr(working[0], 2) ^ std::rotr(working[0], 13) ^ std::rotr(working[0], 22);
            const auto majority = (working[0] & working[1]) ^ (working[0] & working[2]) ^ (working[1] & working[2]);
            for (std::size_t j = 7; j > 0; --j)
            {
                working[j] = working[j - 1];
            }
            working[4] += t1;
            working[0] = t1 + sum0 + majority;
        }
        for (std::size_t i = 0; i < state.size(); ++i)
        {
            state[i] += working[i];
        }
    };
    const auto originalSize = bytes.size();
    while (bytes.size() >= 64)
    {
        compress(std::span<const std::byte, 64>(bytes.data(), 64));
        bytes = bytes.subspan(64);
    }
    std::array<std::byte, 64> tail{};
    std::ranges::copy(bytes, tail.begin());
    tail[bytes.size()] = std::byte{ 0x80 };
    if (bytes.size() >= 56)
    {
        compress(tail);
        tail.fill(std::byte{});
    }
    const auto bitCount = static_cast<std::uint64_t>(originalSize) * 8;
    for (unsigned i = 0; i < 8; ++i)
    {
        tail[63 - i] = static_cast<std::byte>((bitCount >> (i * 8)) & 0xFF);
    }
    compress(tail);
    constexpr std::string_view hex = "0123456789abcdef";
    std::string                result;
    result.reserve(64);
    for (const auto word : state)
    {
        for (int shift = 28; shift >= 0; shift -= 4)
        {
            result += hex[(word >> shift) & 15];
        }
    }
    return result;
}

std::expected<std::vector<std::byte>, MapBundleWriteError> EncodeMapBundleManifest(std::span<const MapBundleArtifact> artifacts, std::string_view toolVersion)
{
    if (const auto valid = validateArtifacts(artifacts); !valid)
    {
        return std::unexpected(valid.error());
    }
    if (!validUtf8(toolVersion))
    {
        return std::unexpected(MapBundleWriteError::InvalidToolVersion);
    }
    std::string text = "{\n  \"schemaVersion\": 1,\n  \"label\": \"intermediate - not playable\",\n  \"toolVersion\": " + jsonString(toolVersion) + ",\n  \"artifacts\": [";
    for (std::size_t i = 0; i < artifacts.size(); ++i)
    {
        const auto& artifact = artifacts[i];
        text += i == 0 ? "\n" : ",\n";
        text += "    {\"name\": " + jsonString(artifact.name) + ", \"size\": " + std::to_string(artifact.bytes.size()) + ", \"sha256\": \"" + CalculateMapBundleSha256(artifact.bytes) + "\"}";
    }
    text += artifacts.empty() ? "]\n}\n" : "\n  ]\n}\n";
    const auto bytes = std::as_bytes(std::span(text.data(), text.size()));
    return std::vector<std::byte>(bytes.begin(), bytes.end());
}

MapBundleFileSession::~MapBundleFileSession()
{
    if (!stagingPath.empty())
    {
        static_cast<void>(RemoveOwnedStaging(stagingPath));
    }
}

std::expected<void, MapBundleWriteError> MapBundleFileSession::CreateDirectoryNew(const std::filesystem::path& staging)
{
    if (!stagingPath.empty() || !validName(staging.filename().string()))
    {
        return std::unexpected(MapBundleWriteError::InvalidStaging);
    }
    const auto safe = safePath(staging);
    if (!safe)
    {
        return std::unexpected(safe.error());
    }
    std::error_code error;
    if (!std::filesystem::is_directory(safe->parent_path(), error) || error)
    {
        return std::unexpected(MapBundleWriteError::InvalidParent);
    }
    if (const auto missing = requireMissing(*safe); !missing)
    {
        return missing;
    }
    if (!std::filesystem::create_directory(*safe, error))
    {
        return std::unexpected(!requireMissing(*safe) ? MapBundleWriteError::DestinationExists : MapBundleWriteError::CreateDirectoryFailed);
    }
    stagingPath = *safe;
    return {};
}

std::expected<void, MapBundleWriteError> MapBundleFileSession::WriteNew(const std::filesystem::path& path, std::span<const std::byte> bytes)
{
    const auto safe = safePath(path);
    if (!safe)
    {
        return std::unexpected(safe.error());
    }
    if (stagingPath.empty() || safe->parent_path() != stagingPath || !validName(safe->filename().string()))
    {
        return std::unexpected(MapBundleWriteError::InvalidStaging);
    }
    for (const auto& file : ownedFiles)
    {
        if (folded(file.filename().string()) == folded(safe->filename().string()))
        {
            return std::unexpected(MapBundleWriteError::DuplicateName);
        }
    }
    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
    {
        return std::unexpected(MapBundleWriteError::WriteFailed);
    }
    if (const auto missing = requireMissing(*safe); !missing)
    {
        return missing;
    }
    // Reserve the ownership record before creation, so failed allocation cannot orphan a file.
    ownedFiles.push_back(*safe);
    std::ofstream output(*safe, std::ios::binary | std::ios::noreplace);
    if (!output)
    {
        ownedFiles.pop_back();
        return std::unexpected(MapBundleWriteError::OpenFailed);
    }
    if (!bytes.empty())
    {
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    if (!output)
    {
        return std::unexpected(MapBundleWriteError::WriteFailed);
    }
    output.close();
    if (!output)
    {
        return std::unexpected(MapBundleWriteError::FinalizationFailed);
    }
    return {};
}

std::expected<std::vector<std::byte>, MapBundleWriteError> MapBundleFileSession::Read(const std::filesystem::path& path) const
{
    const auto safe = safePath(path);
    if (!safe)
    {
        return std::unexpected(safe.error());
    }
    if (stagingPath.empty() || std::ranges::find(ownedFiles, *safe) == ownedFiles.end())
    {
        return std::unexpected(MapBundleWriteError::InvalidStaging);
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(*safe, error)) || error)
    {
        return std::unexpected(MapBundleWriteError::NonRegularFile);
    }
    const auto size = std::filesystem::file_size(*safe, error);
    if (error || size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max()) || size > std::vector<std::byte>().max_size())
    {
        return std::unexpected(MapBundleWriteError::ReadFailed);
    }
    std::ifstream input(*safe, std::ios::binary);
    if (!input)
    {
        return std::unexpected(MapBundleWriteError::OpenFailed);
    }
    std::vector<std::byte> result(static_cast<std::size_t>(size));
    if (!result.empty())
    {
        input.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(result.size()));
    }
    if (!input || input.peek() != std::char_traits<char>::eof() || input.bad())
    {
        return std::unexpected(MapBundleWriteError::ReadFailed);
    }
    return result;
}

std::expected<void, MapBundleWriteError> MapBundleFileSession::PublishDirectoryNew(const std::filesystem::path& staging, const std::filesystem::path& destination)
{
    const auto source = safePath(staging);
    const auto target = safePath(destination);
    if (!source || !target)
    {
        return std::unexpected(!source ? source.error() : target.error());
    }
    if (!validName(target->filename().string()))
    {
        return std::unexpected(MapBundleWriteError::InvalidName);
    }
    if (stagingPath.empty() || *source != stagingPath || target->parent_path() != stagingPath.parent_path())
    {
        return std::unexpected(MapBundleWriteError::InvalidStaging);
    }
    if (const auto missing = requireMissing(*target); !missing)
    {
        return missing;
    }
    // Enumerate only this flat directory to reject additions or links before publication.
    std::error_code                           error;
    std::size_t                               count = 0;
    std::filesystem::directory_iterator       iterator(stagingPath, error);
    const std::filesystem::directory_iterator end;
    while (!error && iterator != end)
    {
        const auto entry = safePath(iterator->path());
        if (!entry)
        {
            return std::unexpected(entry.error());
        }
        if (std::ranges::find(ownedFiles, *entry) == ownedFiles.end() || !iterator->is_regular_file(error) || error)
        {
            return std::unexpected(MapBundleWriteError::NonRegularFile);
        }
        ++count;
        iterator.increment(error);
    }
    if (error || count != ownedFiles.size())
    {
        return std::unexpected(MapBundleWriteError::ReadFailed);
    }
    const auto published = renameNew(stagingPath, *target);
    if (published)
    {
        stagingPath.clear();
        ownedFiles.clear();
    }
    return published;
}

std::expected<void, MapBundleWriteError> MapBundleFileSession::RemoveOwnedStaging(const std::filesystem::path& staging)
{
    const auto safe = safePath(staging);
    if (!safe || stagingPath.empty() || *safe != stagingPath)
    {
        return std::unexpected(MapBundleWriteError::CleanupFailed);
    }
    std::error_code error;
    if (!std::filesystem::is_directory(std::filesystem::symlink_status(stagingPath, error)) || error)
    {
        return std::unexpected(MapBundleWriteError::CleanupFailed);
    }
    for (const auto& file : ownedFiles)
    {
        if (!safePath(file))
        {
            return std::unexpected(MapBundleWriteError::CleanupFailed);
        }
        const auto status = std::filesystem::symlink_status(file, error);
        if (error == std::errc::no_such_file_or_directory || status.type() == std::filesystem::file_type::not_found)
        {
            error.clear();
            continue;
        }
        if (error || !std::filesystem::is_regular_file(status))
        {
            return std::unexpected(MapBundleWriteError::CleanupFailed);
        }
        std::filesystem::remove(file, error);
        if (error)
        {
            return std::unexpected(MapBundleWriteError::CleanupFailed);
        }
    }
    // An unowned entry makes removal fail. Never recurse or remove that entry.
    if (!std::filesystem::remove(stagingPath, error) || error)
    {
        return std::unexpected(MapBundleWriteError::CleanupFailed);
    }
    stagingPath.clear();
    ownedFiles.clear();
    return {};
}

std::expected<std::filesystem::path, MapBundleWriteError> WriteMapBundle(const std::filesystem::path& parent, std::string_view directoryName, std::span<const MapBundleArtifact> artifacts, std::string_view toolVersion)
{
    if (!validName(directoryName))
    {
        return std::unexpected(MapBundleWriteError::InvalidName);
    }
    const auto manifest = EncodeMapBundleManifest(artifacts, toolVersion);
    if (!manifest)
    {
        return std::unexpected(manifest.error());
    }
    for (const auto& artifact : artifacts)
    {
        if (!artifact.verify)
        {
            return std::unexpected(MapBundleWriteError::MissingVerifier);
        }
    }
    const auto root = safePath(parent);
    if (!root)
    {
        return std::unexpected(root.error());
    }
    std::error_code error;
    if (!std::filesystem::is_directory(*root, error) || error)
    {
        return std::unexpected(MapBundleWriteError::InvalidParent);
    }
    const auto destination = *root / std::string(directoryName);
    if (const auto missing = requireMissing(destination); !missing)
    {
        return std::unexpected(missing.error());
    }
    MapBundleFileSession              session;
    static std::atomic<std::uint64_t> sequence{ 0 };
    std::filesystem::path             staging;
    for (;;)
    {
        staging            = *root / (".map-bundle-stage-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(sequence.fetch_add(1)));
        const auto created = session.CreateDirectoryNew(staging);
        if (created)
        {
            break;
        }
        if (created.error() != MapBundleWriteError::DestinationExists)
        {
            return std::unexpected(created.error());
        }
    }
    const auto fail = [&](MapBundleWriteError failure) -> std::expected<std::filesystem::path, MapBundleWriteError>
    {
        return std::unexpected(session.RemoveOwnedStaging(staging) ? failure : MapBundleWriteError::CleanupFailed);
    };
    const auto writeAndRead = [&](const std::filesystem::path& path, std::span<const std::byte> bytes) -> std::expected<std::vector<std::byte>, MapBundleWriteError>
    {
        if (const auto written = session.WriteNew(path, bytes); !written)
        {
            return std::unexpected(written.error());
        }
        auto read = session.Read(path);
        if (!read)
        {
            return read;
        }
        if (!std::ranges::equal(*read, bytes))
        {
            return std::unexpected(MapBundleWriteError::ReadBackMismatch);
        }
        return read;
    };
    for (const auto& artifact : artifacts)
    {
        const auto read = writeAndRead(staging / artifact.name, artifact.bytes);
        if (!read)
        {
            return fail(read.error());
        }
        try
        {
            if (!artifact.verify(*read))
            {
                return fail(MapBundleWriteError::VerificationFailed);
            }
        }
        catch (...)
        {
            return fail(MapBundleWriteError::VerificationFailed);
        }
    }
    if (const auto read = writeAndRead(staging / kMapBundleManifestName, *manifest); !read)
    {
        return fail(read.error());
    }
    if (const auto published = session.PublishDirectoryNew(staging, destination); !published)
    {
        return fail(published.error());
    }
    return destination;
}

std::string_view MapBundleWriteErrorMessage(MapBundleWriteError error)
{
    switch (error)
    {
        case MapBundleWriteError::InvalidName:
            return "Bundle names must be nonempty ASCII filesystem basenames of "
                   "at most 255 bytes, without reserved names or trailing dots or spaces.";
        case MapBundleWriteError::DuplicateName:
            return "Bundle artifact names must be unique after ASCII case folding, including the manifest name.";
        case MapBundleWriteError::InvalidToolVersion:
            return "The tool version must be valid UTF-8.";
        case MapBundleWriteError::MissingVerifier:
            return "Every bundle artifact requires a readback verifier.";
        case MapBundleWriteError::InvalidParent:
            return "The bundle parent must be an existing directory with a valid path.";
        case MapBundleWriteError::ReparsePoint:
            return "Bundle paths must not traverse links or reparse points.";
        case MapBundleWriteError::NonRegularFile:
            return "A bundle entry is not an owned regular file.";
        case MapBundleWriteError::DestinationExists:
            return "The bundle destination already exists.";
        case MapBundleWriteError::CreateDirectoryFailed:
            return "Could not create a fresh bundle staging directory.";
        case MapBundleWriteError::InvalidStaging:
            return "The operation requires this session's owned staging directory or a direct child file.";
        case MapBundleWriteError::OpenFailed:
            return "Could not open a bundle file.";
        case MapBundleWriteError::WriteFailed:
            return "Could not write a bundle file.";
        case MapBundleWriteError::FinalizationFailed:
            return "Could not finalize a bundle file.";
        case MapBundleWriteError::ReadFailed:
            return "Could not read a bundle file or directory.";
        case MapBundleWriteError::ReadBackMismatch:
            return "Bundle readback bytes differ from the supplied bytes.";
        case MapBundleWriteError::VerificationFailed:
            return "A bundle artifact readback verifier failed.";
        case MapBundleWriteError::PublishFailed:
            return "Could not publish the complete bundle directory without replacement.";
        case MapBundleWriteError::CleanupFailed:
            return "Could not remove only the owned staging files and empty directory.";
    }
    return "Unknown map bundle write error.";
}

} // namespace rerevved::tools
