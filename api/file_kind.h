#pragma once

#include <filesystem>
#include <string_view>

namespace rerevved::tools
{

enum class FileKind
{
    Unknown,
    FpkArchive,
    DdsTexture,
    GfxMovie,
    BikVideo,
    Mp3Audio,
    NifContainer,
    MapRecord,
};

[[nodiscard]] FileKind         ClassifyFile(const std::filesystem::path& path);
[[nodiscard]] std::string_view FileKindName(FileKind kind);

} // namespace rerevved::tools
