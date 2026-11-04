#pragma once

#include "dds_document.h"
#include "map_document.h"
#include "mp3_document.h"

#include <string>
#include <vector>

namespace rerevved::tools
{

[[nodiscard]] std::vector<std::string> FormatDdsInspection(const DdsDocument& document);
[[nodiscard]] std::vector<std::string> FormatMp3Inspection(const Mp3Document& document);
[[nodiscard]] std::vector<std::string> FormatMapInspection(const MapDocument& document);
[[nodiscard]] std::vector<std::string> FormatMapByteRows(const MapDocument& document);

} // namespace rerevved::tools
