#include "ui/ui_theme.h"

#include <imgui_internal.h>

#include <cstddef>

extern const unsigned char kInterRegular[];
extern const std::size_t   kInterRegularSize;
extern const unsigned char kInterSemiBold[];
extern const std::size_t   kInterSemiBoldSize;
extern const unsigned char kLucideIcons[];
extern const std::size_t   kLucideIconsSize;

namespace rerevved::tools::ui
{
namespace
{

ImFont* regular = nullptr;
ImFont* strong  = nullptr;

ImFont* addFont(const unsigned char* data, std::size_t size, const char* name)
{
    auto&        atlas = *ImGui::GetIO().Fonts;
    ImFontConfig config;
    // Embedded arrays live for the whole process. The atlas must not free them.
    config.FontDataOwnedByAtlas = false;
    // Inter ships glyphs for private use that would shadow the merged Lucide icons.
    static constexpr ImWchar kPrivateUse[] = { 0xE000, 0xF8FF, 0 };
    config.GlyphExcludeRanges              = kPrivateUse;
    ImFormatString(config.Name, IM_ARRAYSIZE(config.Name), "%s", name);
    auto* font = atlas.AddFontFromMemoryTTF(const_cast<unsigned char*>(data), static_cast<int>(size), size::Body, &config);

    ImFontConfig icons;
    icons.FontDataOwnedByAtlas = false;
    icons.MergeMode            = true;
    icons.GlyphOffset          = ImVec2(0.0F, 2.0F);
    icons.GlyphMinAdvanceX     = size::Body;
    ImFormatString(icons.Name, IM_ARRAYSIZE(icons.Name), "Lucide");
    atlas.AddFontFromMemoryTTF(const_cast<unsigned char*>(kLucideIcons), static_cast<int>(kLucideIconsSize), size::Body, &icons);
    return font;
}

int pushedFonts = 0;

} // namespace

ImVec4 ToVec4(ImU32 color)
{
    return ImGui::ColorConvertU32ToFloat4(color);
}

ImFont* RegularFont()
{
    return regular;
}

ImFont* StrongFont()
{
    return strong;
}

void PushFont(FontRole role)
{
    switch (role)
    {
        case FontRole::Body:
            ImGui::PushFont(regular, size::Body);
            break;
        case FontRole::Strong:
            ImGui::PushFont(strong, size::Body);
            break;
        case FontRole::Title:
            ImGui::PushFont(strong, size::Title);
            break;
        case FontRole::Caption:
            ImGui::PushFont(regular, size::Caption);
            break;
        case FontRole::Icon:
            ImGui::PushFont(regular, size::Hero);
            break;
    }
    ++pushedFonts;
}

void PopFont()
{
    IM_ASSERT(pushedFonts > 0);
    --pushedFonts;
    ImGui::PopFont();
}

void ApplyTheme(float dpiScale)
{
    auto& io = ImGui::GetIO();
    io.Fonts->Clear();
    regular        = addFont(kInterRegular, kInterRegularSize, "Inter Regular");
    strong         = addFont(kInterSemiBold, kInterSemiBoldSize, "Inter SemiBold");
    io.FontDefault = regular;

    ImGuiStyle style;
    ImGui::StyleColorsDark(&style);
    style.FontSizeBase            = size::Body;
    style.FontScaleDpi            = dpiScale;
    style.WindowPadding           = ImVec2(16.0F, 16.0F);
    style.FramePadding            = ImVec2(10.0F, 6.0F);
    style.ItemSpacing             = ImVec2(8.0F, 8.0F);
    style.ItemInnerSpacing        = ImVec2(8.0F, 6.0F);
    style.CellPadding             = ImVec2(8.0F, 6.0F);
    style.IndentSpacing           = 18.0F;
    style.ScrollbarSize           = 12.0F;
    style.GrabMinSize             = 10.0F;
    style.WindowRounding          = 0.0F;
    style.ChildRounding           = 0.0F;
    style.FrameRounding           = 2.0F;
    style.PopupRounding           = 2.0F;
    style.ScrollbarRounding       = 2.0F;
    style.GrabRounding            = 2.0F;
    style.TabRounding             = 0.0F;
    style.WindowBorderSize        = 0.0F;
    style.ChildBorderSize         = 1.0F;
    style.PopupBorderSize         = 1.0F;
    style.FrameBorderSize         = 1.0F;
    style.SeparatorTextBorderSize = 1.0F;
    style.SeparatorTextPadding    = ImVec2(0.0F, 4.0F);
    style.WindowTitleAlign        = ImVec2(0.0F, 0.5F);
    style.SelectableTextAlign     = ImVec2(0.0F, 0.5F);

    auto* colors                               = style.Colors;
    colors[ImGuiCol_Text]                      = ToVec4(color::Text);
    colors[ImGuiCol_TextDisabled]              = ToVec4(color::Faint);
    colors[ImGuiCol_WindowBg]                  = ToVec4(color::Sunken);
    colors[ImGuiCol_ChildBg]                   = ToVec4(IM_COL32(0, 0, 0, 0));
    colors[ImGuiCol_PopupBg]                   = ToVec4(color::Raised);
    colors[ImGuiCol_Border]                    = ToVec4(color::Border);
    colors[ImGuiCol_BorderShadow]              = ToVec4(IM_COL32(0, 0, 0, 0));
    colors[ImGuiCol_FrameBg]                   = ToVec4(IM_COL32(14, 42, 108, 255));
    colors[ImGuiCol_FrameBgHovered]            = ToVec4(IM_COL32(20, 56, 136, 255));
    colors[ImGuiCol_FrameBgActive]             = ToVec4(IM_COL32(20, 56, 136, 255));
    colors[ImGuiCol_TitleBg]                   = ToVec4(color::Surface);
    colors[ImGuiCol_TitleBgActive]             = ToVec4(color::Surface);
    colors[ImGuiCol_TitleBgCollapsed]          = ToVec4(color::Surface);
    colors[ImGuiCol_MenuBarBg]                 = ToVec4(color::Surface);
    colors[ImGuiCol_ScrollbarBg]               = ToVec4(IM_COL32(0, 0, 0, 0));
    colors[ImGuiCol_ScrollbarGrab]             = ToVec4(color::Border);
    colors[ImGuiCol_ScrollbarGrabHovered]      = ToVec4(IM_COL32(104, 142, 204, 255));
    colors[ImGuiCol_ScrollbarGrabActive]       = ToVec4(color::Accent);
    colors[ImGuiCol_CheckMark]                 = ToVec4(color::Accent);
    colors[ImGuiCol_SliderGrab]                = ToVec4(color::Accent);
    colors[ImGuiCol_SliderGrabActive]          = ToVec4(color::Text);
    colors[ImGuiCol_Button]                    = ToVec4(color::Raised);
    colors[ImGuiCol_ButtonHovered]             = ToVec4(color::Hover);
    colors[ImGuiCol_ButtonActive]              = ToVec4(color::AccentFill);
    colors[ImGuiCol_Header]                    = ToVec4(color::AccentSoft);
    colors[ImGuiCol_HeaderHovered]             = ToVec4(color::Hover);
    colors[ImGuiCol_HeaderActive]              = ToVec4(color::AccentFill);
    colors[ImGuiCol_Separator]                 = ToVec4(color::Border);
    colors[ImGuiCol_SeparatorHovered]          = ToVec4(color::Accent);
    colors[ImGuiCol_SeparatorActive]           = ToVec4(color::Accent);
    colors[ImGuiCol_ResizeGrip]                = ToVec4(IM_COL32(0, 0, 0, 0));
    colors[ImGuiCol_ResizeGripHovered]         = ToVec4(color::Accent);
    colors[ImGuiCol_ResizeGripActive]          = ToVec4(color::Accent);
    colors[ImGuiCol_Tab]                       = ToVec4(color::Surface);
    colors[ImGuiCol_TabHovered]                = ToVec4(color::Hover);
    colors[ImGuiCol_TabSelected]               = ToVec4(color::Raised);
    colors[ImGuiCol_TabSelectedOverline]       = ToVec4(color::Accent);
    colors[ImGuiCol_TabDimmed]                 = ToVec4(color::Surface);
    colors[ImGuiCol_TabDimmedSelected]         = ToVec4(color::Raised);
    colors[ImGuiCol_TabDimmedSelectedOverline] = ToVec4(IM_COL32(0, 0, 0, 0));
    colors[ImGuiCol_DockingPreview]            = ToVec4(color::AccentSoft);
    colors[ImGuiCol_TableHeaderBg]             = ToVec4(color::Surface);
    colors[ImGuiCol_TableBorderStrong]         = ToVec4(color::Border);
    colors[ImGuiCol_TableBorderLight]          = ToVec4(IM_COL32(36, 70, 140, 255));
    colors[ImGuiCol_TableRowBg]                = ToVec4(IM_COL32(0, 0, 0, 0));
    colors[ImGuiCol_TableRowBgAlt]             = ToVec4(IM_COL32(255, 255, 255, 6));
    colors[ImGuiCol_TextSelectedBg]            = ToVec4(IM_COL32(31, 95, 196, 200));
    colors[ImGuiCol_NavCursor]                 = ToVec4(color::Accent);
    colors[ImGuiCol_ModalWindowDimBg]          = ToVec4(IM_COL32(4, 10, 26, 170));
    colors[ImGuiCol_DockingEmptyBg]            = ToVec4(IM_COL32(0, 0, 0, 0));
    ImGui::GetStyle()                          = style;
}

} // namespace rerevved::tools::ui
