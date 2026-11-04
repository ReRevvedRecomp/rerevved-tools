#pragma once

#include "file_kind.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>

namespace rerevved::tools
{

constexpr std::size_t kInspectionHeaderSize = 16;

struct FileInspection
{
    std::filesystem::path                        path;
    FileKind                                     kind = FileKind::Unknown;
    std::uintmax_t                               size = 0;
    std::array<std::byte, kInspectionHeaderSize> header{};
    std::size_t                                  headerSize = 0;
};

[[nodiscard]] std::expected<FileInspection, std::string>
InspectFile(const std::filesystem::path& path);

} // namespace rerevved::tools
