#include "file_inspection.h"

#include <fstream>
#include <system_error>

namespace rerevved::tools
{

std::expected<FileInspection, std::string>
InspectFile(const std::filesystem::path& path)
{
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error))
    {
        if (error)
        {
            return std::unexpected("Could not inspect path: " + error.message());
        }
        return std::unexpected("Path is not a regular file.");
    }

    const auto size = std::filesystem::file_size(path, error);
    if (error)
    {
        return std::unexpected("Could not read file size: " + error.message());
    }

    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        return std::unexpected("Could not open file for reading.");
    }

    FileInspection inspection{
        .path = path,
        .kind = ClassifyFile(path),
        .size = size,
    };
    input.read(reinterpret_cast<char*>(inspection.header.data()),
               static_cast<std::streamsize>(inspection.header.size()));
    inspection.headerSize = static_cast<std::size_t>(input.gcount());
    return inspection;
}

} // namespace rerevved::tools
