#pragma once

#include "mp3_document.h"

#include <string_view>

namespace rerevved::tools
{

void DrawMp3Preview(const Mp3Document* document, std::string_view error);

} // namespace rerevved::tools
