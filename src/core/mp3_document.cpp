#include "mp3_document.h"

#include <dr_mp3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <string_view>
#include <utility>

namespace rerevved::tools
{
namespace
{

// Decoder API from dr_mp3 0.7.3, pinned at:
// https://github.com/mackron/dr_libs/tree/5690d4671d7ad07ae6021756d7222eb159745f06

constexpr std::uint64_t kPreviewSeconds  = 10;
constexpr std::uint32_t kPointsPerSecond = 100;
constexpr std::size_t   kMaximumChannels = 2;

bool hasCompleteFrameStream(std::span<const std::byte> bytes, const drmp3& decoder)
{
    if (decoder.streamStartOffset >= decoder.streamLength ||
        decoder.streamLength > bytes.size())
    {
        return false;
    }

    drmp3dec frameDecoder{};
    drmp3dec_init(&frameDecoder);
    auto       offset = static_cast<std::size_t>(decoder.streamStartOffset);
    const auto end    = static_cast<std::size_t>(decoder.streamLength);
    while (offset < end)
    {
        const auto remaining = end - offset;
        if (remaining < 2 || bytes[offset] != std::byte{ 0xFF } ||
            (std::to_integer<unsigned int>(bytes[offset + 1]) & 0xE0U) != 0xE0U)
        {
            return false;
        }

        drmp3dec_frame_info info{};
        const auto          inputSize = static_cast<int>(std::min<std::size_t>(
            remaining, static_cast<std::size_t>(std::numeric_limits<int>::max())));
        const auto          frames    = drmp3dec_decode_frame(
            &frameDecoder,
            reinterpret_cast<const drmp3_uint8*>(bytes.data() + offset),
            inputSize,
            nullptr,
            &info);
        if (frames <= 0 || info.frame_bytes <= 0 ||
            static_cast<std::size_t>(info.frame_bytes) > remaining)
        {
            return false;
        }
        offset += static_cast<std::size_t>(info.frame_bytes);
    }
    return offset == end;
}

} // namespace

std::string_view Mp3DocumentErrorMessage(Mp3DocumentError error)
{
    switch (error)
    {
        case Mp3DocumentError::DecodeFailed:
            return "The MP3 audio could not be decoded.";
    }
    return "The MP3 audio is invalid.";
}

std::expected<Mp3Document, Mp3DocumentError>
ParseMp3Document(std::span<const std::byte> bytes)
{
    drmp3 decoder{};
    if (!drmp3_init_memory(&decoder, bytes.data(), bytes.size(), nullptr))
    {
        return std::unexpected(Mp3DocumentError::DecodeFailed);
    }
    if (!hasCompleteFrameStream(bytes, decoder))
    {
        drmp3_uninit(&decoder);
        return std::unexpected(Mp3DocumentError::DecodeFailed);
    }

    Mp3Document document{
        .sampleRate        = decoder.sampleRate,
        .channels          = decoder.channels,
        .previewFrameCount = 0,
        .previewComplete   = false,
        .waveform          = {},
    };
    const auto maximumFrames =
        static_cast<std::uint64_t>(document.sampleRate) * kPreviewSeconds;
    document.waveform.reserve(kPointsPerSecond * kPreviewSeconds);

    std::array<float, DRMP3_MAX_PCM_FRAMES_PER_MP3_FRAME * kMaximumChannels> pcm{};
    std::uint64_t                                                            framesSincePoint = 0;
    std::uint64_t                                                            nextPointFrame =
        (document.sampleRate + kPointsPerSecond - 1U) / kPointsPerSecond;
    float bucketPeak  = 0.0F;
    float bucketValue = 0.0F;
    while (document.previewFrameCount < maximumFrames)
    {
        const auto framesToRead = std::min<std::uint64_t>(
            DRMP3_MAX_PCM_FRAMES_PER_MP3_FRAME,
            maximumFrames - document.previewFrameCount);
        const auto framesRead =
            drmp3_read_pcm_frames_f32(&decoder, framesToRead, pcm.data());
        if (framesRead == 0)
        {
            document.previewComplete = true;
            break;
        }

        for (std::uint64_t frame = 0; frame < framesRead; ++frame)
        {
            for (std::uint32_t channel = 0; channel < document.channels; ++channel)
            {
                const auto value =
                    pcm[static_cast<std::size_t>(frame) * document.channels + channel];
                if (!std::isfinite(value))
                {
                    drmp3_uninit(&decoder);
                    return std::unexpected(Mp3DocumentError::DecodeFailed);
                }
                if (std::abs(value) > bucketPeak)
                {
                    bucketPeak  = std::abs(value);
                    bucketValue = value;
                }
            }
            ++document.previewFrameCount;
            ++framesSincePoint;
            if (document.previewFrameCount == nextPointFrame)
            {
                document.waveform.push_back(bucketValue);
                framesSincePoint = 0;
                bucketPeak       = 0.0F;
                bucketValue      = 0.0F;
                nextPointFrame =
                    ((document.waveform.size() + 1) *
                         static_cast<std::uint64_t>(document.sampleRate) +
                     kPointsPerSecond - 1U) /
                    kPointsPerSecond;
            }
        }
    }
    drmp3_uninit(&decoder);

    if (framesSincePoint != 0)
    {
        document.waveform.push_back(bucketValue);
    }
    if (document.previewFrameCount == 0)
    {
        return std::unexpected(Mp3DocumentError::DecodeFailed);
    }
    return document;
}

std::expected<Mp3Document, std::string>
LoadMp3Document(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
    {
        return std::unexpected("Could not open MP3 file for reading.");
    }

    const auto end = input.tellg();
    if (end < 0)
    {
        return std::unexpected("Could not read MP3 file size.");
    }
    const auto size = static_cast<std::uintmax_t>(end);
    if (size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max()))
    {
        return std::unexpected("MP3 file exceeds the supported size range.");
    }

    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input && !input.eof())
    {
        return std::unexpected("Could not read MP3 file data.");
    }
    if (static_cast<std::size_t>(input.gcount()) != bytes.size())
    {
        return std::unexpected("MP3 file ended before its reported size.");
    }

    auto document = ParseMp3Document(bytes);
    if (!document)
    {
        return std::unexpected(std::string(Mp3DocumentErrorMessage(document.error())));
    }
    return std::move(*document);
}

} // namespace rerevved::tools
