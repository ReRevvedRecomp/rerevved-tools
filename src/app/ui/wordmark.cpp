#include "ui/wordmark.h"
#include "texture_import.h"

#include <GLFW/glfw3.h>

#include <cstddef>
#include <span>

extern const unsigned char kRerevvedWordmark[];
extern const std::size_t   kRerevvedWordmarkSize;

namespace rerevved::tools
{

Wordmark::~Wordmark()
{
    if (texture != 0)
    {
        glDeleteTextures(1, &texture);
    }
}

float Wordmark::Draw(ImDrawList& drawList, ImVec2 position, float height)
{
    if (!tried)
    {
        tried            = true;
        const auto bytes = std::span(reinterpret_cast<const std::byte*>(kRerevvedWordmark), kRerevvedWordmarkSize);
        if (const auto image = ImportTexture(bytes, TextureSourceKind::Png); image && !image->rgba8.empty())
        {
            glGenTextures(1, &texture);
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(image->width), static_cast<GLsizei>(image->height), 0, GL_RGBA, GL_UNSIGNED_BYTE, image->rgba8.data());
            aspect = static_cast<float>(image->width) / static_cast<float>(image->height);
        }
    }
    if (texture == 0)
    {
        return 0.0F;
    }
    const float width = height * aspect;
    drawList.AddImage(static_cast<ImTextureID>(texture), position, ImVec2(position.x + width, position.y + height));
    return width;
}

} // namespace rerevved::tools
