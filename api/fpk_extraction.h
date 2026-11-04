#pragma once

#include "fpk_index.h"

#include <cstddef>
#include <expected>
#include <filesystem>
#include <span>

namespace rerevved::tools
{

enum class FpkExtractionError
{
    InvalidSelection,
    InvalidEntryRange,
    DestinationExists,
    OpenFailed,
    WriteFailed,
    FinalizationFailed,
};

[[nodiscard]] std::expected<void, FpkExtractionError>
ExtractFpkEntry(std::span<const std::byte>   source,
                const FpkIndex&              index,
                std::size_t                  selectedEntry,
                const std::filesystem::path& destination);

} // namespace rerevved::tools
