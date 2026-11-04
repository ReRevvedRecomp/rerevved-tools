#pragma once

#include "map/scenario_form.h"

namespace rerevved::tools
{

void DrawScenarioDetails(CreatorWorkspace& workspace, ScenarioEditorState& state, const MapDocument* map);
// Xbox variator levels.
void DrawScenarioRules(CreatorWorkspace& workspace, ScenarioEditorState& state, const MapDocument* map);
void DrawScenarioAdvanced(CreatorWorkspace& workspace, ScenarioEditorState& state, const MapDocument* map);

} // namespace rerevved::tools
