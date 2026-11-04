#include "inspect/dds_viewer.h"
#include "ui/ui_theme.h"
#include "ui/ui_widgets.h"

#include <imgui.h>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

namespace rerevved::tools
{

DdsViewer::~DdsViewer()
{
    Clear();
}

void DdsViewer::Clear()
{
    releaseTexture();
    documentSource.clear();
    mipLevel = 0;
}

void DdsViewer::releaseTexture()
{
    if (texture != 0)
    {
        glDeleteTextures(1, &texture);
    }
    texture = 0;
    loadedSource.clear();
    uploadError.clear();
}

void DdsViewer::Draw(const DdsDocument* document,
                     std::string_view   sourceId,
                     std::string_view   error,
                     DdsPayloadLayout*  layout)
{
    if (layout != nullptr)
    {
        // The choice stays available after a failed read so a wrong layout can be undone.
        constexpr std::array<const char*, 2> kLayouts{ "Linear", "Xbox tiled" };
        int                                  selected = *layout == DdsPayloadLayout::XboxTiledBigEndian ? 1 : 0;
        if (ui::Segmented("dds-layout", kLayouts, selected))
        {
            *layout = selected == 1 ? DdsPayloadLayout::XboxTiledBigEndian : DdsPayloadLayout::LinearLittleEndian;
        }
        ImGui::SameLine();
        ui::InfoMarker("DDS headers do not record Xbox tiling. "
                       "Pick the layout used to write the file. "
                       "The wrong one shows scrambled pixels or an error.");
        ui::VerticalSpace(4.0F);
    }
    if (document == nullptr)
    {
        Clear();
        ui::EmptyState("dds-failure", ui::icon::XCircle, "This texture could not be read", error, nullptr, nullptr, std::max(160.0F, ImGui::GetContentRegionAvail().y));
        return;
    }

    if (documentSource != sourceId)
    {
        documentSource = sourceId;
        mipLevel       = 0;
    }
    const std::size_t levelCount = std::max<std::size_t>(document->mipLevels.size(), 1);
    mipLevel                     = std::min(mipLevel, levelCount - 1);
    // Documents without decoded levels still carry their top level in rgba8.
    const bool hasLevels  = !document->mipLevels.empty();
    const auto levelWidth = [&]
    {
        return hasLevels ? document->mipLevels[mipLevel].width : document->metadata.width;
    };
    const auto levelHeight = [&]
    {
        return hasLevels ? document->mipLevels[mipLevel].height : document->metadata.height;
    };

    ui::Heading(std::to_string(levelWidth()) + " x " + std::to_string(levelHeight()));
    const char* layoutName = document->payloadLayout == DdsPayloadLayout::XboxTiledBigEndian
                                 ? "Xbox tiled big endian"
                                 : "Linear little endian";
    ui::Caption(std::string(layoutName) + "  -  " + std::to_string(levelCount) + (levelCount == 1 ? " level" : " levels"));
    if (levelCount > 1)
    {
        if (ui::IconButton("previous-level", ui::icon::ChevronLeft, "Previous level", false, mipLevel > 0))
        {
            --mipLevel;
        }
        ImGui::SameLine();
        if (ui::IconButton("next-level", ui::icon::ChevronRight, "Next level", false, mipLevel + 1 < levelCount))
        {
            ++mipLevel;
        }
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ui::Muted("Level " + std::to_string(mipLevel));
    }
    if (document->mipIssue)
    {
        ui::TonedText(ui::Tone::Warn, "Mip chain: " + std::string(DdsDocumentErrorMessage(*document->mipIssue)));
    }
    ui::VerticalSpace(4.0F);
    // Controls come first so a level that cannot be presented can still be stepped past.
    const auto uploadKey = std::string(sourceId) + "#mip" + std::to_string(mipLevel);
    if (loadedSource != uploadKey)
    {
        upload(hasLevels ? std::span<const std::byte>(document->mipLevels[mipLevel].rgba8) : std::span<const std::byte>(document->rgba8), levelWidth(), levelHeight(), uploadKey);
    }
    if (!uploadError.empty())
    {
        ui::EmptyState("dds-presentation", ui::icon::XCircle, "Presentation Failed", uploadError, nullptr, nullptr, std::max(160.0F, ImGui::GetContentRegionAvail().y));
        return;
    }
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 stage{ std::max(available.x, 1.0F), std::max(available.y - 4.0F, 120.0F) };
    const float  width  = static_cast<float>(levelWidth());
    const float  height = static_cast<float>(levelHeight());
    const float  scale  = std::min((stage.x - 24.0F) / width, (stage.y - 24.0F) / height);
    const ImVec2 imageSize{ std::max(1.0F, width * scale), std::max(1.0F, height * scale) };
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 limit{ origin.x + stage.x, origin.y + stage.y };
    auto*        drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(origin, limit, ui::color::Surface, 10.0F);
    const ImVec2 imageMin{ origin.x + (stage.x - imageSize.x) * 0.5F, origin.y + (stage.y - imageSize.y) * 0.5F };
    const ImVec2 imageMax{ imageMin.x + imageSize.x, imageMin.y + imageSize.y };
    drawList->PushClipRect(imageMin, imageMax, true);
    constexpr float kChecker = 8.0F;
    for (float y = imageMin.y; y < imageMax.y; y += kChecker)
    {
        for (float x = imageMin.x; x < imageMax.x; x += kChecker)
        {
            const bool dark = (static_cast<int>((x - imageMin.x) / kChecker) + static_cast<int>((y - imageMin.y) / kChecker)) % 2 == 0;
            drawList->AddRectFilled({ x, y }, { x + kChecker, y + kChecker }, dark ? IM_COL32(44, 48, 56, 255) : IM_COL32(58, 63, 72, 255));
        }
    }
    drawList->PopClipRect();
    drawList->AddImage(static_cast<ImTextureID>(texture), imageMin, imageMax);
    drawList->AddRect(origin, limit, ui::color::Border, 10.0F);
    ImGui::Dummy(stage);
}

void DdsViewer::upload(std::span<const std::byte> pixels, std::uint32_t width, std::uint32_t height, std::string_view sourceId)
{
    releaseTexture();
    loadedSource = sourceId;

    GLint maximumTextureSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximumTextureSize);
    if (width == 0 || height == 0 || static_cast<std::uint64_t>(width) * height * 4 != pixels.size())
    {
        uploadError = "Decoded texture pixels are unavailable or inconsistent.";
        return;
    }
    if (width > static_cast<std::uint32_t>(maximumTextureSize) ||
        height > static_cast<std::uint32_t>(maximumTextureSize))
    {
        uploadError = "DDS dimensions exceed the OpenGL texture limit.";
        return;
    }

    // Isolate this upload from errors left by other presentation work. Bound the loop
    // because a lost context can report errors indefinitely.
    for (int drained = 0; drained < 32 && glGetError() != GL_NO_ERROR; ++drained)
    {
    }
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RGBA8,
                 static_cast<GLsizei>(width),
                 static_cast<GLsizei>(height),
                 0,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 pixels.data());
    if (glGetError() != GL_NO_ERROR)
    {
        uploadError = "OpenGL could not upload the decoded texture pixels.";
    }
}

} // namespace rerevved::tools
