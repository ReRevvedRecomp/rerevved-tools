#include "inspect/inspection_format.h"

#include <cstddef>
#include <iomanip>
#include <span>
#include <sstream>

namespace rerevved::tools
{

std::string FormatBytes(std::span<const std::byte> bytes)
{
    std::ostringstream output;
    output << std::hex << std::uppercase << std::setfill('0');
    for (std::size_t index = 0; index < bytes.size(); ++index)
    {
        if (index != 0)
        {
            output << ' ';
        }
        output << std::setw(2) << std::to_integer<unsigned int>(bytes[index]);
    }
    return output.str();
}

std::string FormatHeader(const FileInspection& inspection)
{
    return FormatBytes(std::span(inspection.header).first(inspection.headerSize));
}

std::string FormatByteSize(std::uintmax_t bytes)
{
    constexpr std::uintmax_t kKibibyte = 1024;
    constexpr std::uintmax_t kMebibyte = kKibibyte * 1024;
    constexpr std::uintmax_t kGibibyte = kMebibyte * 1024;

    std::ostringstream output;
    output << std::fixed << std::setprecision(2);
    if (bytes >= kGibibyte)
    {
        output << static_cast<double>(bytes) / static_cast<double>(kGibibyte) << " GiB";
    }
    else if (bytes >= kMebibyte)
    {
        output << static_cast<double>(bytes) / static_cast<double>(kMebibyte) << " MiB";
    }
    else if (bytes >= kKibibyte)
    {
        output << static_cast<double>(bytes) / static_cast<double>(kKibibyte) << " KiB";
    }
    else
    {
        output << bytes << " bytes";
    }
    return output.str();
}

std::string FormatFpkEntryRange(std::size_t oneBasedEntry, const FpkEntry& entry)
{
    const auto end = static_cast<std::uint64_t>(entry.offset) + entry.size;

    std::ostringstream output;
    output << "Entry: " << oneBasedEntry << '\n';
    output << "Offset: " << entry.offset << " (0x" << std::hex << std::uppercase
           << std::setfill('0') << std::setw(8) << entry.offset << ")\n";
    output << std::dec << "Size: " << entry.size << " bytes (0x" << std::hex
           << std::setw(8) << entry.size << ")\n";
    output << std::dec << "End (exclusive): " << end << " (0x" << std::hex
           << std::setw(8) << end << ')';
    return output.str();
}

std::string FormatGfxBytes(std::span<const std::byte> bytes)
{
    if (bytes.empty())
    {
        return "<empty>";
    }

    std::ostringstream output;
    output << std::hex << std::uppercase << std::setfill('0');
    for (const auto value : bytes)
    {
        const auto character = std::to_integer<unsigned int>(value);
        if (character >= 0x20 && character <= 0x7E && character != '\\')
        {
            output << static_cast<char>(character);
        }
        else if (character == '\\')
        {
            output << "\\\\";
        }
        else
        {
            output << "\\x" << std::setw(2) << character;
        }
    }
    return output.str();
}

} // namespace rerevved::tools
