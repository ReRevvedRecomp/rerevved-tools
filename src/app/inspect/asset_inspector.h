#pragma once

#include "dds_document.h"
#include "inspect/inspector_navigation.h"
#include "map_document.h"
#include "mp3_document.h"

namespace rerevved::tools
{

void DrawDdsInspector(const DdsDocument& document);
void DrawMp3Inspector(const Mp3Document& document);
void DrawMapInspector(const MapDocument& document, InspectorNavigationState& state);

} // namespace rerevved::tools
