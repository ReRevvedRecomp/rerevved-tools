#pragma once

#include "dds_document.h"
#include "texture_import.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace rerevved::tools
{

class DdsViewer
{
public:
    DdsViewer() = default;
    ~DdsViewer();

    DdsViewer(const DdsViewer&)            = delete;
    DdsViewer& operator=(const DdsViewer&) = delete;

    void Clear();
    // With a layout, also offers the payload layout choice and writes the user's pick back.
    void Draw(const DdsDocument* document, std::string_view sourceId, std::string_view error, DdsPayloadLayout* layout = nullptr);

    [[nodiscard]] std::string_view Error() const
    {
        return uploadError;
    }

private:
    void releaseTexture();
    void upload(std::span<const std::byte> pixels, std::uint32_t width, std::uint32_t height, std::string_view sourceId);

    unsigned int texture = 0;
    std::string  loadedSource;
    std::string  documentSource;
    std::size_t  mipLevel = 0;
    std::string  uploadError;
};

} // namespace rerevved::tools
