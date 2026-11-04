#include "file_kind.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace rerevved::tools
{
namespace
{

std::string lowercaseExtension(const std::filesystem::path& path)
{
    auto extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char value)
                           {
                               return static_cast<char>(std::tolower(value));
                           });
    return extension;
}

} // namespace

FileKind ClassifyFile(const std::filesystem::path& path)
{
    const auto extension = lowercaseExtension(path);
    if (extension == ".fpk")
    {
        return FileKind::FpkArchive;
    }
    if (extension == ".dds")
    {
        return FileKind::DdsTexture;
    }
    if (extension == ".gfx")
    {
        return FileKind::GfxMovie;
    }
    if (extension == ".bik")
    {
        return FileKind::BikVideo;
    }
    if (extension == ".mp3")
    {
        return FileKind::Mp3Audio;
    }
    if (extension == ".nif")
    {
        return FileKind::NifContainer;
    }
    if (extension == ".map")
    {
        return FileKind::MapRecord;
    }
    return FileKind::Unknown;
}

std::string_view FileKindName(FileKind kind)
{
    switch (kind)
    {
        case FileKind::FpkArchive:
            return "FPK archive";
        case FileKind::DdsTexture:
            return "DDS texture";
        case FileKind::GfxMovie:
            return "GFx movie";
        case FileKind::BikVideo:
            return "Bink video";
        case FileKind::Mp3Audio:
            return "MP3 audio";
        case FileKind::NifContainer:
            return "Gamebryo NIF";
        case FileKind::MapRecord:
            return "Xbox DLC map";
        case FileKind::Unknown:
            return "Unknown";
    }
    return "Unknown";
}

} // namespace rerevved::tools
