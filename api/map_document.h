#pragma once

#include <array>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace rerevved::tools
{

inline constexpr std::size_t kMapWidth        = 32;
inline constexpr std::size_t kMapHeight       = 32;
inline constexpr std::size_t kMapCoreSize     = kMapWidth * kMapHeight;
inline constexpr std::size_t kMapFooterSize   = 64;
inline constexpr std::size_t kMapEnvelopeSize = kMapCoreSize + kMapFooterSize;

enum class MapDocumentError
{
    Truncated,
    UnsupportedLength,
};

struct MapDocument
{
    std::array<std::byte, kMapCoreSize>   core;
    std::array<std::byte, kMapFooterSize> footer;
};

[[nodiscard]] std::string_view MapDocumentErrorMessage(MapDocumentError error);

[[nodiscard]] std::expected<MapDocument, MapDocumentError>
ParseMapDocument(std::span<const std::byte> bytes);

[[nodiscard]] std::expected<MapDocument, std::string>
LoadMapDocument(const std::filesystem::path& path);

} // namespace rerevved::tools
