#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rerevved::tools
{

enum class Mp3DocumentError
{
    DecodeFailed,
};

struct Mp3Document
{
    std::uint32_t      sampleRate        = 0;
    std::uint32_t      channels          = 0;
    std::uint64_t      previewFrameCount = 0;
    bool               previewComplete   = false;
    std::vector<float> waveform;
};

[[nodiscard]] std::string_view Mp3DocumentErrorMessage(Mp3DocumentError error);

[[nodiscard]] std::expected<Mp3Document, Mp3DocumentError>
ParseMp3Document(std::span<const std::byte> bytes);

[[nodiscard]] std::expected<Mp3Document, std::string>
LoadMp3Document(const std::filesystem::path& path);

} // namespace rerevved::tools
