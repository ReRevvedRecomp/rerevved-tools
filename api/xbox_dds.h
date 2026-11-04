#pragma once

#include "dds_document.h"
#include "dds_writer.h"
#include "xbox_texture.h"

#include <expected>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace rerevved::tools
{

enum class XboxDdsWriteError
{
    NoLevels,
    LevelDimensionMismatch,
    LayoutRequiresPowerOfTwo,
    UnsupportedFormat,
    PackedMipTailUnsupported,
    SizeOverflow,
    PixelLimitExceeded,
    DestinationExists,
    OpenFailed,
    WriteFailed,
    FinalizationFailed,
    ReadBackFailed,
};

struct XboxDdsSpec
{
    XboxTextureFormat format        = XboxTextureFormat::Argb8;
    DdsPayloadLayout  payloadLayout = DdsPayloadLayout::LinearLittleEndian;
};

[[nodiscard]] std::string_view XboxDdsWriteErrorMessage(XboxDdsWriteError error);

// Levels are explicit, tightly packed RGBA8 images. Source provenance is ignored.
[[nodiscard]] std::expected<std::vector<std::byte>, XboxDdsWriteError>
EncodeXboxDds(std::span<const Rgba8Image> levels, const XboxDdsSpec& spec);

// Count includes the source level. Straight RGBA channels use rounding with halves rounded up.
// One pixel axes replicate their edge. No color space or alpha weighting occurs.
[[nodiscard]] std::expected<std::vector<Rgba8Image>, XboxDdsWriteError>
GenerateXboxMipLevels(const Rgba8Image& image, std::uint32_t count);

[[nodiscard]] std::expected<void, XboxDdsWriteError>
WriteXboxDds(std::span<const Rgba8Image>  levels,
             const XboxDdsSpec&           spec,
             const std::filesystem::path& destination);

namespace detail
{

// Checks dimensions without allocating for the levels selected for decoding.
[[nodiscard]] std::expected<void, DdsPreviewError>
ValidateDdsPixelBudget(const DdsMetadata& metadata, std::uint32_t levelCount);

using DdsMipDescriber = decltype(&DescribeMipChain);
[[nodiscard]] std::expected<DdsDocument, DdsDocumentError>
ParseDdsDocumentWithMipDescriber(std::span<const std::byte> bytes, DdsPayloadLayout layout, DdsMipDescriber describe);

using DdsPublisher = decltype(&PublishDds);
[[nodiscard]] std::expected<void, XboxDdsWriteError>
WriteXboxDdsWithPublisher(std::span<const Rgba8Image>  levels,
                          const XboxDdsSpec&           spec,
                          const std::filesystem::path& destination,
                          DdsPublisher                 publish);

} // namespace detail

} // namespace rerevved::tools
