#pragma once

#include <imgui.h>

namespace rerevved::tools
{

// Uploaded on its first draw, while the OpenGL context is current.
class Wordmark
{
public:
    Wordmark() = default;
    ~Wordmark();

    Wordmark(const Wordmark&)            = delete;
    Wordmark& operator=(const Wordmark&) = delete;

    // Returns the width, or 0 when it could not load.
    float Draw(ImDrawList& drawList, ImVec2 position, float height);

private:
    unsigned int texture = 0;
    float        aspect  = 0.0F;
    bool         tried   = false;
};

} // namespace rerevved::tools
