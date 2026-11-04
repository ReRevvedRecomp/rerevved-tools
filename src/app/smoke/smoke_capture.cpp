#include "smoke/smoke_capture.h"

#include "texture_import.h"

#include <GLFW/glfw3.h>
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <format>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>
#include <system_error>
#include <vector>

namespace rerevved::tools
{

namespace
{

struct CaptureState
{
    int          session;
    unsigned int frame;
    const char*  filename;
};

constexpr std::array kCaptureStates{
    CaptureState{ 0, 1, "00-map-start.png" },
    CaptureState{ 0, 3, "01-files.png" },
    CaptureState{ 0, 5, "02-map-grid.png" },
    CaptureState{ 0, 7, "03-map-export.png" },
    CaptureState{ 0, 9, "04-map-export-images.png" },
    CaptureState{ 0, 11, "05-map-export-step.png" },
    CaptureState{ 0, 13, "06-discard-sheet.png" },
    CaptureState{ 0, 15, "07-shell-final.png" },
    CaptureState{ 1, 1, "08-restart.png" }
};

void writePng(void* context, void* data, int size)
{
    static_cast<std::ofstream*>(context)->write(static_cast<const char*>(data), size);
}

std::expected<Rgba8Image, std::string> readCapture(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        return std::unexpected(std::format("cannot open {}", path.string()));
    }
    std::vector<char>      text{ std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
    std::vector<std::byte> bytes(text.size());
    std::ranges::transform(text, bytes.begin(), [](char value)
                           {
                               return static_cast<std::byte>(value);
                           });
    auto image = ImportTexture(bytes, TextureSourceKind::Png);
    if (!image)
    {
        return std::unexpected(std::format("cannot decode {}: {}", path.string(), TextureImportErrorMessage(image.error())));
    }
    return std::move(*image);
}

std::set<std::string> captureNames(const std::filesystem::path& directory)
{
    std::set<std::string> names;
    std::error_code       error;
    for (const auto& entry : std::filesystem::directory_iterator(directory, error))
    {
        if (entry.is_regular_file(error) && entry.path().extension() == ".png")
        {
            names.insert(entry.path().filename().string());
        }
    }
    return names;
}

} // namespace

std::string_view SmokeCaptureErrorMessage(SmokeCaptureError error)
{
    switch (error)
    {
        case SmokeCaptureError::DirectoryUnavailable:
            return "Capture directory must already exist and be accessible.";
        case SmokeCaptureError::InvalidFramebufferSize:
            return "Capture framebuffer size is invalid or too large.";
        case SmokeCaptureError::ReadFailed:
            return "Capture back buffer read failed.";
        case SmokeCaptureError::OpenFailed:
            return "Cannot exclusively create capture file. It may already exist.";
        case SmokeCaptureError::WriteFailed:
            return "PNG encoding, writing or finalization failed. Partial capture retained.";
    }
    return "Unknown smoke capture error.";
}

std::expected<void, SmokeCaptureError> CheckSmokeCaptureDirectory(const std::filesystem::path& directory)
{
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error) || error)
    {
        return std::unexpected(SmokeCaptureError::DirectoryUnavailable);
    }
    return {};
}

std::expected<void, SmokeCaptureError> CaptureSmokeFrame(const std::filesystem::path& directory, int session, unsigned int frame, int width, int height)
{
    const auto state = std::ranges::find_if(kCaptureStates, [=](const CaptureState& candidate)
                                            {
                                                return candidate.session == session && candidate.frame == frame;
                                            });
    if (state == kCaptureStates.end())
    {
        return {};
    }
    return CaptureNamedSmokeFrame(directory, state->filename, width, height);
}

std::expected<void, SmokeCaptureError> CaptureNamedSmokeFrame(const std::filesystem::path& directory, std::string_view filename, int width, int height)
{
    if (width <= 0 || height <= 0 || width > std::numeric_limits<int>::max() / 4 || static_cast<std::size_t>(height) > std::numeric_limits<std::size_t>::max() / (static_cast<std::size_t>(width) * 4))
    {
        return std::unexpected(SmokeCaptureError::InvalidFramebufferSize);
    }

    const auto                 rowBytes = static_cast<std::size_t>(width) * 4;
    std::vector<unsigned char> pixels(rowBytes * static_cast<std::size_t>(height));
    GLint                      readBuffer    = 0;
    GLint                      packAlignment = 0;
    glGetIntegerv(GL_READ_BUFFER, &readBuffer);
    glGetIntegerv(GL_PACK_ALIGNMENT, &packAlignment);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    // Stale errors from earlier rendering must not fail this read.
    for (int drained = 0; drained < 32 && glGetError() != GL_NO_ERROR; ++drained)
    {
    }
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    const auto readError = glGetError();
    glPixelStorei(GL_PACK_ALIGNMENT, packAlignment);
    glReadBuffer(static_cast<GLenum>(readBuffer));
    if (readError != GL_NO_ERROR)
    {
        return std::unexpected(SmokeCaptureError::ReadFailed);
    }
    for (int row = 0; row < height / 2; ++row)
    {
        auto* top    = pixels.data() + static_cast<std::size_t>(row) * rowBytes;
        auto* bottom = pixels.data() + static_cast<std::size_t>(height - row - 1) * rowBytes;
        std::swap_ranges(top, top + rowBytes, bottom);
    }

    const auto    destination = directory / filename;
    std::ofstream output(destination, std::ios::binary | std::ios::noreplace);
    if (!output)
    {
        std::fprintf(stderr, "Cannot create smoke capture %s\n", destination.string().c_str());
        return std::unexpected(SmokeCaptureError::OpenFailed);
    }
    const bool encoded = stbi_write_png_to_func(writePng, &output, width, height, 4, pixels.data(), static_cast<int>(rowBytes)) != 0;
    output.close();
    if (!encoded || output.fail())
    {
        return std::unexpected(SmokeCaptureError::WriteFailed);
    }
    std::fprintf(stdout, "Smoke capture: %s (%dx%d)\n", destination.string().c_str(), width, height);
    return {};
}

std::expected<bool, std::string> CompareSmokeCaptures(const std::filesystem::path& baseline, const std::filesystem::path& candidate, const std::filesystem::path& diffDirectory)
{
    for (const auto* directory : { &baseline, &candidate, &diffDirectory })
    {
        if (!CheckSmokeCaptureDirectory(*directory))
        {
            return std::unexpected(std::format("{} is not an existing directory", directory->string()));
        }
    }
    // Highlights go only into an empty directory that is neither input, so no result mixes with old output.
    std::error_code error;
    if (!std::filesystem::is_empty(diffDirectory, error) || error)
    {
        return std::unexpected(std::format("{} is not empty", diffDirectory.string()));
    }
    for (const auto* input : { &baseline, &candidate })
    {
        if (std::filesystem::equivalent(*input, diffDirectory, error) || error)
        {
            return std::unexpected(std::format("{} must differ from both capture directories", diffDirectory.string()));
        }
    }
    const auto baselineNames  = captureNames(baseline);
    const auto candidateNames = captureNames(candidate);
    if (baselineNames.empty() && candidateNames.empty())
    {
        return std::unexpected(std::string("neither directory contains PNG captures"));
    }
    bool matched = baselineNames == candidateNames;
    for (const auto& name : baselineNames)
    {
        if (!candidateNames.contains(name))
        {
            std::fprintf(stdout, "missing  %s\n", name.c_str());
        }
    }
    for (const auto& name : candidateNames)
    {
        if (!baselineNames.contains(name))
        {
            std::fprintf(stdout, "added    %s\n", name.c_str());
            continue;
        }
        const auto before = readCapture(baseline / name);
        const auto after  = readCapture(candidate / name);
        if (!before || !after)
        {
            return std::unexpected(!before ? before.error() : after.error());
        }
        if (before->width != after->width || before->height != after->height)
        {
            std::fprintf(stdout, "resized  %s: %ux%u -> %ux%u\n", name.c_str(), before->width, before->height, after->width, after->height);
            matched = false;
            continue;
        }
        std::vector<unsigned char> highlight(after->rgba8.size());
        std::size_t                changed = 0;
        int                        largest = 0;
        std::uint32_t              left = after->width, top = after->height, right = 0, bottom = 0;
        for (std::size_t pixel = 0; pixel < after->rgba8.size() / 4; ++pixel)
        {
            int delta = 0;
            for (std::size_t channel = 0; channel < 4; ++channel)
            {
                delta = std::max(delta, std::abs(std::to_integer<int>(before->rgba8[pixel * 4 + channel]) - std::to_integer<int>(after->rgba8[pixel * 4 + channel])));
            }
            auto* out = highlight.data() + pixel * 4;
            if (delta == 0)
            {
                for (std::size_t channel = 0; channel < 3; ++channel)
                {
                    out[channel] = static_cast<unsigned char>(std::to_integer<int>(after->rgba8[pixel * 4 + channel]) * 3 / 10);
                }
                out[3] = 255;
                continue;
            }
            ++changed;
            largest      = std::max(largest, delta);
            const auto x = static_cast<std::uint32_t>(pixel % after->width);
            const auto y = static_cast<std::uint32_t>(pixel / after->width);
            left         = std::min(left, x);
            top          = std::min(top, y);
            right        = std::max(right, x);
            bottom       = std::max(bottom, y);
            constexpr std::array kMagenta{ 255, 0, 255, 255 };
            std::ranges::copy(kMagenta, out);
        }
        if (changed == 0)
        {
            std::fprintf(stdout, "same     %s\n", name.c_str());
            continue;
        }
        matched = false;
        std::fprintf(stdout, "changed  %s: %.3f%% of pixels, max delta %d, box (%u,%u)-(%u,%u)\n", name.c_str(), 100.0 * static_cast<double>(changed) / static_cast<double>(after->rgba8.size() / 4), largest, left, top, right, bottom);
        const auto    destination = diffDirectory / name;
        std::ofstream output(destination, std::ios::binary | std::ios::noreplace);
        const auto    width   = static_cast<int>(after->width);
        const bool    written = output && stbi_write_png_to_func(writePng, &output, width, static_cast<int>(after->height), 4, highlight.data(), width * 4) != 0;
        output.close();
        if (!written || !output)
        {
            return std::unexpected(std::format("cannot write {}", destination.string()));
        }
    }
    return matched;
}

} // namespace rerevved::tools
