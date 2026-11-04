#pragma once

#include "texture_import.h"

#include <cstddef>
#include <expected>
#include <filesystem>
#include <functional>
#include <span>
#include <string_view>
#include <vector>

namespace rerevved::tools
{

enum class DdsWriteError
{
    UnsupportedContract,
    ContractMismatch,
    DestinationExists,
    OpenFailed,
    WriteFailed,
    FinalizationFailed,
    ReadBackFailed,
};

[[nodiscard]] std::string_view DdsWriteErrorMessage(DdsWriteError error);

[[nodiscard]] std::expected<std::vector<std::byte>, DdsWriteError>
EncodeTextureDds(const Rgba8Image& image, const TextureKeyContract& contract);

// The volume must support hard links.
[[nodiscard]] std::expected<void, DdsWriteError>
WriteTextureDds(const Rgba8Image&            image,
                const TextureKeyContract&    contract,
                const std::filesystem::path& destination);

namespace detail
{

[[nodiscard]] std::expected<void, DdsWriteError>
PublishDds(std::span<const std::byte>                             bytes,
           const std::filesystem::path&                           destination,
           const std::function<bool(std::span<const std::byte>)>& verify);

} // namespace detail

} // namespace rerevved::tools
