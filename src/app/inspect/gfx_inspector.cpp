#include "inspect/gfx_inspector.h"
#include "inspect/inspection_format.h"
#include "inspect/inspector_navigation_ui.h"
#include "ui/ui_widgets.h"

#include <imgui.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rerevved::tools
{
namespace
{

void drawWrappedField(std::string_view label, std::string_view value)
{
    ui::KeyValue(label, value, 136.0F);
}

} // namespace

void DrawGfxInspector(const GfxDocument*        document,
                      std::string_view          error,
                      InspectorNavigationState& state)
{
    ui::Heading("GFX movie");
    if (!document)
    {
        ui::TonedText(ui::Tone::Error, error.empty() ? std::string_view("The GFX movie could not be inspected.") : error);
        return;
    }

    const auto widthTwips  = static_cast<std::int64_t>(document->frame.xMaxTwips) -
                             document->frame.xMinTwips;
    const auto heightTwips = static_cast<std::int64_t>(document->frame.yMaxTwips) -
                             document->frame.yMinTwips;
    ui::FieldF("External images: %zu", document->externalImages.size());
    ui::Caption("Metadata only. External images are not loaded.");
    if (ui::Section("Movie Details"))
    {
        ui::FieldF("File version: %u", static_cast<unsigned int>(document->fileVersion));
        ui::FieldF("Declared length: %u bytes",
                   static_cast<unsigned int>(document->declaredLength));
        ui::FieldF("Frame X: %d to %d twips",
                   document->frame.xMinTwips,
                   document->frame.xMaxTwips);
        ui::FieldF("Frame Y: %d to %d twips",
                   document->frame.yMinTwips,
                   document->frame.yMaxTwips);
        ui::FieldF("Frame size: %.2f x %.2f pixels",
                   static_cast<double>(widthTwips) / 20.0,
                   static_cast<double>(heightTwips) / 20.0);
        ui::FieldF("Frame rate: %.2f (%u raw)",
                   static_cast<double>(document->frameRateRaw) / 256.0,
                   static_cast<unsigned int>(document->frameRateRaw));
        ui::FieldF("Frame count: %u", static_cast<unsigned int>(document->frameCount));
    }

    if (ui::Section("Exporter Details and Raw Bytes"))
    {
        ui::FieldF("Version: %u.%u",
                   static_cast<unsigned int>(document->exporter.version >> 8U),
                   static_cast<unsigned int>(document->exporter.version & 0xFFU));
        ui::FieldF("Flags: 0x%08X", static_cast<unsigned int>(document->exporter.flags));
        ui::FieldF("Bitmap format: %u",
                   static_cast<unsigned int>(document->exporter.bitmapFormat));
        drawWrappedField("Prefix", FormatGfxBytes(document->exporter.prefix));
        drawWrappedField("SWF name", FormatGfxBytes(document->exporter.swfName));
    }

    ui::VerticalSpace(6.0F);
    ui::Heading("External images");
    if (document->externalImages.empty())
    {
        ui::Muted("None");
        return;
    }

    std::vector<std::string> labels;
    labels.reserve(document->externalImages.size());
    for (std::size_t index = 0; index < document->externalImages.size(); ++index)
    {
        const auto& image = document->externalImages[index];
        labels.push_back("Resource " + std::to_string(index + 1) + ": " +
                         "export " + FormatGfxBytes(image.exportName) + ", file " +
                         FormatGfxBytes(image.fileName));
    }

    const auto selected = DrawInspectorNavigation(labels, state, "gfx-external-images", 1);
    if (!selected || *selected >= document->externalImages.size())
    {
        return;
    }

    const auto& image = document->externalImages[*selected];
    ui::Divider();
    ui::FieldF("Original resource ordinal: %zu", *selected + 1);
    drawWrappedField("Export name", FormatGfxBytes(image.exportName));
    drawWrappedField("File name", FormatGfxBytes(image.fileName));
    ui::FieldF("Character ID: %u", static_cast<unsigned int>(image.characterId));
    ui::FieldF("Bitmap format: %u", static_cast<unsigned int>(image.bitmapFormat));
    ui::FieldF("Target size: %u x %u",
               static_cast<unsigned int>(image.targetWidth),
               static_cast<unsigned int>(image.targetHeight));
}

} // namespace rerevved::tools
