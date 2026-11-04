#include "fpk_extraction.h"

#include <cstdint>
#include <fstream>
#include <limits>
#include <system_error>

namespace rerevved::tools
{

std::expected<void, FpkExtractionError>
ExtractFpkEntry(std::span<const std::byte>   source,
                const FpkIndex&              index,
                std::size_t                  selectedEntry,
                const std::filesystem::path& destination)
{
    if (selectedEntry >= index.entries.size())
    {
        return std::unexpected(FpkExtractionError::InvalidSelection);
    }

    const auto& entry = index.entries[selectedEntry];
    if (entry.size > std::numeric_limits<std::uint32_t>::max() - entry.offset)
    {
        return std::unexpected(FpkExtractionError::InvalidEntryRange);
    }

    const auto offset = static_cast<std::size_t>(entry.offset);
    const auto size   = static_cast<std::size_t>(entry.size);
    if (offset > source.size() || size > source.size() - offset)
    {
        return std::unexpected(FpkExtractionError::InvalidEntryRange);
    }

    std::ofstream output(destination, std::ios::binary | std::ios::noreplace);
    if (!output)
    {
        std::error_code error;
        const auto      destinationStatus = std::filesystem::symlink_status(destination, error);
        if (!error && destinationStatus.type() != std::filesystem::file_type::not_found)
        {
            return std::unexpected(FpkExtractionError::DestinationExists);
        }
        return std::unexpected(FpkExtractionError::OpenFailed);
    }

    const auto payload = source.subspan(offset, size);
    output.write(reinterpret_cast<const char*>(payload.data()),
                 static_cast<std::streamsize>(payload.size()));
    if (!output)
    {
        return std::unexpected(FpkExtractionError::WriteFailed);
    }

    output.close();
    if (output.fail())
    {
        return std::unexpected(FpkExtractionError::FinalizationFailed);
    }
    return {};
}

} // namespace rerevved::tools
