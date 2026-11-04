#include "inspect/mp3_preview.h"
#include "ui/ui_theme.h"
#include "ui/ui_widgets.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>

namespace rerevved::tools
{

void DrawMp3Preview(const Mp3Document* document, std::string_view error)
{
    if (!document)
    {
        ui::EmptyState("mp3-failure", ui::icon::XCircle, "This audio could not be previewed", error.empty() ? std::string_view("The MP3 audio could not be previewed.") : error, nullptr, nullptr, std::max(160.0F, ImGui::GetContentRegionAvail().y));
        return;
    }

    std::array<char, 96> window{};
    std::snprintf(window.data(),
                  window.size(),
                  "Waveform window: %.2f seconds%s",
                  static_cast<double>(document->previewFrameCount) / document->sampleRate,
                  document->previewComplete ? "" : " (limited)");
    ui::Heading(std::to_string(document->sampleRate) + " Hz, " + std::to_string(document->channels) + (document->channels == 1 ? " channel" : " channels"));
    ui::Caption(window.data());
    ui::VerticalSpace(4.0F);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ui::ToVec4(ui::color::Surface));
    ImGui::PushStyleColor(ImGuiCol_PlotLines, ui::ToVec4(ui::color::Accent));
    ImGui::PushStyleColor(ImGuiCol_PlotLinesHovered, ui::ToVec4(ui::color::Text));
    ImGui::PushStyleColor(ImGuiCol_Border, ui::ToVec4(ui::color::Border));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0F, 12.0F));
    ImGui::PlotLines("##mp3-waveform",
                     document->waveform.data(),
                     static_cast<int>(document->waveform.size()),
                     0,
                     nullptr,
                     -1.0F,
                     1.0F,
                     ImVec2(-1.0F, std::clamp(ImGui::GetContentRegionAvail().y - 40.0F, 180.0F, 360.0F)));
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(4);
    ui::Caption("Read only decoded amplitude preview. Playback is not supported.");
}

} // namespace rerevved::tools
