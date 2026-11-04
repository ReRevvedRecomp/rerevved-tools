#pragma once
#include "inspect/document_workspace.h"
#include "ui/ui_widgets.h"
#include <imgui.h>
#include <string>
#include <string_view>

namespace rerevved::tools
{

inline void DrawDocumentContext(std::string_view source, const DocumentStatus& status)
{
    const bool failed = !status.error.empty();
    ui::TonedText(failed ? ui::Tone::Error : status.stage == DocumentStage::Ready ? ui::Tone::Ok
                                                                                  : ui::Tone::Neutral,
                  status.label);
    if (failed)
    {
        const auto stage = DocumentStageName(status.stage);
        ui::Caption(std::string(stage) + ": " + status.error);
    }
    ui::Caption(source);
    ui::VerticalSpace(4.0F);
}

} // namespace rerevved::tools
