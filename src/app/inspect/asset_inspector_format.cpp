#include "inspect/asset_inspector_format.h"
#include "inspect/inspection_format.h"

#include <format>
#include <string_view>

namespace rerevved::tools
{
namespace
{

std::string rawValue(std::string_view label, std::uint32_t value)
{
    return std::format("{}: {} (0x{:08X})", label, value, value);
}

} // namespace

std::vector<std::string> FormatDdsInspection(const DdsDocument& document)
{
    const auto&              metadata = document.metadata;
    const auto&              pixels   = metadata.pixelFormat;
    std::vector<std::string> fields{
        std::format("Dimensions: {} x {}", metadata.width, metadata.height),
        rawValue("Flags", metadata.flags),
        rawValue("Pitch or linear size", metadata.pitchOrLinearSize),
        rawValue("Depth", metadata.depth),
        rawValue("Mip map count", metadata.mipMapCount),
        "Reserved 1: " + FormatBytes(metadata.reserved1),
        rawValue("Pixel format flags", pixels.flags),
        "FourCC bytes: " + FormatBytes(pixels.fourCc),
        rawValue("RGB bit count", pixels.rgbBitCount),
        rawValue("Red mask", pixels.redMask),
        rawValue("Green mask", pixels.greenMask),
        rawValue("Blue mask", pixels.blueMask),
        rawValue("Alpha mask", pixels.alphaMask),
        rawValue("Caps", metadata.caps),
        rawValue("Caps 2", metadata.caps2),
        "Caps reserved: " + FormatBytes(metadata.capsReserved),
        "Reserved 2: " + FormatBytes(metadata.reserved2),
        std::format("Decoded RGBA8 bytes: {}", document.rgba8.size())
    };
    if (metadata.dx10Header)
    {
        const auto& dx10 = *metadata.dx10Header;
        fields.push_back(rawValue("DX10 DXGI format", dx10.dxgiFormat));
        fields.push_back(rawValue("DX10 resource dimension", dx10.resourceDimension));
        fields.push_back(rawValue("DX10 misc flag", dx10.miscFlag));
        fields.push_back(rawValue("DX10 array size", dx10.arraySize));
        fields.push_back(rawValue("DX10 misc flags 2", dx10.miscFlags2));
    }
    return fields;
}

std::vector<std::string> FormatMp3Inspection(const Mp3Document& document)
{
    return {
        std::format("Sample rate: {} Hz", document.sampleRate),
        std::format("Channels: {}", document.channels),
        std::format("Preview frame count: {}", document.previewFrameCount),
        std::string("Preview complete: ") + (document.previewComplete ? "yes" : "no (limited)"),
        std::format("Waveform samples: {}", document.waveform.size())
    };
}

std::vector<std::string> FormatMapInspection(const MapDocument& document)
{
    std::vector<std::string> fields{
        std::format("Core: {} bytes, file range [0, {})", document.core.size(), document.core.size()),
        std::format("Footer: {} bytes, file range [{}, {})", document.footer.size(), document.core.size(), document.core.size() + document.footer.size()),
        "Raw bytes in file order. Terrain and orientation meanings are unresolved."
    };
    return fields;
}

std::vector<std::string> FormatMapByteRows(const MapDocument& document)
{
    std::vector<std::string> rows;
    rows.reserve(document.core.size() + document.footer.size());
    const auto append = [&rows](std::span<const std::byte> bytes, std::string_view region)
    {
        for (const auto value : bytes)
        {
            const auto offset = rows.size();
            const auto raw    = std::to_integer<unsigned int>(value);
            rows.push_back(std::format("Offset {} (0x{:04X}) / {} / {} (0x{:02X})", offset, offset, region, raw, raw));
        }
    };
    append(document.core, "core");
    append(document.footer, "footer");
    return rows;
}

} // namespace rerevved::tools
