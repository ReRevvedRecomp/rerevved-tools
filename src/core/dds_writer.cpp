#include "dds_writer.h"

#include "dds_document.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <fstream>
#include <string>
#include <system_error>

namespace rerevved::tools
{
namespace
{

void writeU32Le(std::span<std::byte> bytes, std::size_t offset, std::uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8)
    {
        bytes[offset + shift / 8] = static_cast<std::byte>((value >> shift) & 0xFFU);
    }
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

bool isSupportedContract(const TextureKeyContract& contract)
{
    const auto logo = MainMenuLogoTextureContract();
    return contract.key == logo.key && contract.width == logo.width &&
           contract.height == logo.height && contract.encoding == logo.encoding &&
           contract.mipLevelCount == logo.mipLevelCount &&
           contract.requiresAlphaChannel == logo.requiresAlphaChannel;
}

} // namespace

std::string_view DdsWriteErrorMessage(DdsWriteError error)
{
    switch (error)
    {
        case DdsWriteError::UnsupportedContract:
            return "DDS writing supports only the main menu logo texture contract.";
        case DdsWriteError::ContractMismatch:
            return "The image does not satisfy the texture key contract.";
        case DdsWriteError::DestinationExists:
            return "The DDS destination already exists.";
        case DdsWriteError::OpenFailed:
            return "Could not create a temporary DDS file beside the destination.";
        case DdsWriteError::WriteFailed:
            return "Could not write the complete DDS file.";
        case DdsWriteError::FinalizationFailed:
            return "Could not finalize the new DDS file. The destination volume "
                   "must support hard links.";
        case DdsWriteError::ReadBackFailed:
            return "DDS parser readback did not match the source image exactly.";
    }
    return "The DDS file could not be written.";
}

std::expected<std::vector<std::byte>, DdsWriteError>
EncodeTextureDds(const Rgba8Image& image, const TextureKeyContract& contract)
{
    if (!isSupportedContract(contract))
    {
        return std::unexpected(DdsWriteError::UnsupportedContract);
    }
    if (const auto* dds = std::get_if<DdsSourceLayout>(&image.sourceLayout); dds && dds->decodeError)
    {
        return std::unexpected(DdsWriteError::ContractMismatch);
    }
    if (!ValidateTexture(image, contract).empty())
    {
        return std::unexpected(DdsWriteError::ContractMismatch);
    }

    std::vector<std::byte> bytes(128 + image.rgba8.size());
    bytes[0] = std::byte{ 'D' };
    bytes[1] = std::byte{ 'D' };
    bytes[2] = std::byte{ 'S' };
    bytes[3] = std::byte{ ' ' };
    writeU32Le(bytes, 4, 124);
    writeU32Le(bytes, 8, 0x100FU);
    writeU32Le(bytes, 12, contract.height);
    writeU32Le(bytes, 16, contract.width);
    writeU32Le(bytes, 20, contract.width * 4);
    writeU32Le(bytes, 24, 0);
    writeU32Le(bytes, 28, 0);
    writeU32Le(bytes, 76, 32);
    writeU32Le(bytes, 80, 0x41U);
    writeU32Le(bytes, 84, 0);
    writeU32Le(bytes, 88, 32);
    writeU32Le(bytes, 92, 0x00FF0000U);
    writeU32Le(bytes, 96, 0x0000FF00U);
    writeU32Le(bytes, 100, 0x000000FFU);
    writeU32Le(bytes, 104, 0xFF000000U);
    writeU32Le(bytes, 108, 0x1000U);
    writeU32Le(bytes, 112, 0);
    for (std::size_t offset = 0; offset < image.rgba8.size(); offset += 4)
    {
        bytes[128 + offset]     = image.rgba8[offset + 2];
        bytes[128 + offset + 1] = image.rgba8[offset + 1];
        bytes[128 + offset + 2] = image.rgba8[offset];
        bytes[128 + offset + 3] = image.rgba8[offset + 3];
    }
    return bytes;
}

std::expected<void, DdsWriteError>
WriteTextureDds(const Rgba8Image&            image,
                const TextureKeyContract&    contract,
                const std::filesystem::path& destination)
{
    const auto bytes = EncodeTextureDds(image, contract);
    if (!bytes)
    {
        return std::unexpected(bytes.error());
    }
    return detail::PublishDds(*bytes, destination, [&](std::span<const std::byte> readBackBytes)
                              {
                                  const auto readBack = ParseDdsDocument(readBackBytes);
                                  return readBack && readBack->metadata.width == image.width &&
                                         readBack->metadata.height == image.height && readBack->rgba8 == image.rgba8;
                              });
}

std::expected<void, DdsWriteError>
detail::PublishDds(std::span<const std::byte>                             bytes,
                   const std::filesystem::path&                           destination,
                   const std::function<bool(std::span<const std::byte>)>& verify)
{
    std::error_code error;
    const auto      destinationStatus = std::filesystem::symlink_status(destination, error);
    if (destinationStatus.type() != std::filesystem::file_type::not_found && !error)
    {
        return std::unexpected(DdsWriteError::DestinationExists);
    }
    if (error && error != std::errc::no_such_file_or_directory)
    {
        return std::unexpected(DdsWriteError::OpenFailed);
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
            return std::unexpected(DdsWriteError::OpenFailed);
        }
        output.clear();
    }
    if (!output.is_open())
    {
        return std::unexpected(DdsWriteError::OpenFailed);
    }
    const TemporaryFile cleanup{ temporary };
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!output)
    {
        output.close();
        return std::unexpected(DdsWriteError::WriteFailed);
    }
    output.close();
    if (output.fail())
    {
        return std::unexpected(DdsWriteError::FinalizationFailed);
    }

    std::ifstream          input(temporary, std::ios::binary);
    std::vector<std::byte> readBackBytes(bytes.size());
    input.read(reinterpret_cast<char*>(readBackBytes.data()), static_cast<std::streamsize>(readBackBytes.size()));
    if (!input || input.peek() != std::char_traits<char>::eof() || input.bad() || !std::ranges::equal(readBackBytes, bytes))
    {
        return std::unexpected(DdsWriteError::ReadBackFailed);
    }
    if (!verify(readBackBytes))
    {
        return std::unexpected(DdsWriteError::ReadBackFailed);
    }
    input.close();

    error.clear();
    std::filesystem::create_hard_link(temporary, destination, error);
    if (error)
    {
        std::error_code statusError;
        const auto      status = std::filesystem::symlink_status(destination, statusError);
        if (!statusError && status.type() != std::filesystem::file_type::not_found)
        {
            return std::unexpected(DdsWriteError::DestinationExists);
        }
        return std::unexpected(DdsWriteError::FinalizationFailed);
    }
    return {};
}

} // namespace rerevved::tools
