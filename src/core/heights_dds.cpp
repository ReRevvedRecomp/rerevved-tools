#include "heights_dds.h"

#include "dds_writer.h"

#include <algorithm>
#include <array>
#include <span>
#include <utility>
#include <vector>

namespace rerevved::tools
{

namespace
{

using Error = HeightsDdsError;

// The complete header shared byte for byte by every shipped heights file.
constexpr std::array<std::byte, 128> kHeader = []
{
    std::array<std::byte, 128> bytes{};
    for (const auto [offset, value] : std::array{
             std::pair{ 0U, 0x20534444U },
             std::pair{ 4U, 124U },
             std::pair{ 8U, 0x1007U },
             std::pair{ 12U, kHeightsDdsSide },
             std::pair{ 16U, kHeightsDdsSide },
             std::pair{ 76U, 32U },
             std::pair{ 80U, 0x20000U },
             std::pair{ 88U, 16U },
             std::pair{ 92U, 0xFFFFU },
             std::pair{ 108U, 0x1000U } })
    {
        for (unsigned i = 0; i < 4; ++i)
        {
            bytes[offset + i] = static_cast<std::byte>((value >> (i * 8)) & 255U);
        }
    }
    return bytes;
}();

Error publicationError(DdsWriteError error)
{
    switch (error)
    {
        case DdsWriteError::DestinationExists:
            return Error::DestinationExists;
        case DdsWriteError::OpenFailed:
            return Error::OpenFailed;
        case DdsWriteError::WriteFailed:
            return Error::WriteFailed;
        case DdsWriteError::FinalizationFailed:
            return Error::FinalizationFailed;
        case DdsWriteError::ReadBackFailed:
        case DdsWriteError::UnsupportedContract:
        case DdsWriteError::ContractMismatch:
            return Error::ReadBackFailed;
    }
    return Error::ReadBackFailed;
}

} // namespace

std::string_view HeightsDdsErrorMessage(HeightsDdsError error)
{
    switch (error)
    {
        case Error::SampleCountMismatch:
            return "Heights need exactly 512 x 512 samples.";
        case Error::LengthMismatch:
            return "Heights DDS length is not 128 + 512 x 512 x 2 bytes.";
        case Error::HeaderMismatch:
            return "Heights DDS header differs from the retail 512 x 512 16 bit layout.";
        case Error::DestinationExists:
            return DdsWriteErrorMessage(DdsWriteError::DestinationExists);
        case Error::OpenFailed:
            return DdsWriteErrorMessage(DdsWriteError::OpenFailed);
        case Error::WriteFailed:
            return DdsWriteErrorMessage(DdsWriteError::WriteFailed);
        case Error::FinalizationFailed:
            return DdsWriteErrorMessage(DdsWriteError::FinalizationFailed);
        case Error::ReadBackFailed:
            return "Heights DDS readback did not match the samples.";
    }
    return "Unknown heights DDS error.";
}

std::expected<std::vector<std::byte>, HeightsDdsError>
EncodeHeightsDds(std::span<const std::uint16_t> samples)
{
    if (samples.size() != kHeightsDdsSampleCount)
    {
        return std::unexpected(Error::SampleCountMismatch);
    }
    std::vector<std::byte> bytes(kHeightsDdsByteCount);
    std::ranges::copy(kHeader, bytes.begin());
    auto out = bytes.begin() + kHeader.size();
    for (const auto sample : samples)
    {
        *out++ = static_cast<std::byte>(sample & 255U);
        *out++ = static_cast<std::byte>(sample >> 8);
    }
    return bytes;
}

std::expected<std::vector<std::uint16_t>, HeightsDdsError>
ParseHeightsDds(std::span<const std::byte> bytes)
{
    if (bytes.size() != kHeightsDdsByteCount)
    {
        return std::unexpected(Error::LengthMismatch);
    }
    if (!std::ranges::equal(bytes.first(kHeader.size()), kHeader))
    {
        return std::unexpected(Error::HeaderMismatch);
    }
    std::vector<std::uint16_t> samples(kHeightsDdsSampleCount);
    const auto                 payload = bytes.subspan(kHeader.size());
    for (std::size_t i = 0; i < samples.size(); ++i)
    {
        samples[i] = static_cast<std::uint16_t>(std::to_integer<unsigned>(payload[i * 2]) |
                                                (std::to_integer<unsigned>(payload[i * 2 + 1]) << 8));
    }
    return samples;
}

std::expected<void, HeightsDdsError>
WriteHeightsDds(std::span<const std::uint16_t> samples, const std::filesystem::path& destination)
{
    const auto bytes = EncodeHeightsDds(samples);
    if (!bytes)
    {
        return std::unexpected(bytes.error());
    }
    const auto published = detail::PublishDds(*bytes, destination, [&](std::span<const std::byte> readBack)
                                              {
                                                  const auto parsed = ParseHeightsDds(readBack);
                                                  return parsed && std::ranges::equal(*parsed, samples);
                                              });
    if (!published)
    {
        return std::unexpected(publicationError(published.error()));
    }
    return {};
}

} // namespace rerevved::tools
