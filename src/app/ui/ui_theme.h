#pragma once

#include <imgui.h>

namespace rerevved::tools::ui
{

// Lucide 0.460.0 codepoints. The icon font is merged into every Inter font.
namespace icon
{

inline constexpr const char* Activity     = "\xEE\x80\xB8";
inline constexpr const char* Alert        = "\xEE\x86\x93";
inline constexpr const char* Archive      = "\xEE\x81\x85";
inline constexpr const char* Ban          = "\xEE\x81\x95";
inline constexpr const char* Box          = "\xEE\x81\xA5";
inline constexpr const char* Brush        = "\xEE\x87\x93";
inline constexpr const char* Check        = "\xEE\x81\xB0";
inline constexpr const char* CheckCircle  = "\xEE\x88\xA6";
inline constexpr const char* ChevronDown  = "\xEE\x81\xB1";
inline constexpr const char* ChevronLeft  = "\xEE\x81\xB2";
inline constexpr const char* ChevronRight = "\xEE\x81\xB3";
inline constexpr const char* CircleDot    = "\xEE\x8D\x89";
inline constexpr const char* Clock        = "\xEE\x82\x8B";
inline constexpr const char* Close        = "\xEE\x86\xB2";
inline constexpr const char* Copy         = "\xEE\x82\xA2";
inline constexpr const char* Download     = "\xEE\x82\xB6";
inline constexpr const char* Eraser       = "\xEE\x8A\x8F";
inline constexpr const char* ExternalLink = "\xEE\x82\xBD";
inline constexpr const char* Eye          = "\xEE\x82\xBE";
inline constexpr const char* File         = "\xEE\x83\x84";
inline constexpr const char* FileArchive  = "\xEE\x8C\x8D";
inline constexpr const char* FileAudio    = "\xEE\x8C\x8E";
inline constexpr const char* FileImage    = "\xEE\x8C\x9D";
inline constexpr const char* FileText     = "\xEE\x83\x90";
inline constexpr const char* FileUp       = "\xEE\x8C\xAB";
inline constexpr const char* Flag         = "\xEE\x83\x96";
inline constexpr const char* Folder       = "\xEE\x83\x9C";
inline constexpr const char* FolderOpen   = "\xEE\x89\x87";
inline constexpr const char* Grid         = "\xEE\x83\xAC";
inline constexpr const char* Hand         = "\xEE\x87\x97";
inline constexpr const char* Help         = "\xEE\x82\x82";
inline constexpr const char* Image        = "\xEE\x83\xB9";
inline constexpr const char* Info         = "\xEE\x83\xBE";
inline constexpr const char* Layers       = "\xEE\x84\x84";
inline constexpr const char* ListChecks   = "\xEE\x87\x90";
inline constexpr const char* Lock         = "\xEE\x84\x8F";
inline constexpr const char* Map          = "\xEE\x84\x94";
inline constexpr const char* MapPin       = "\xEE\x84\x95";
inline constexpr const char* Minus        = "\xEE\x84\xA0";
inline constexpr const char* Package      = "\xEE\x84\xAD";
inline constexpr const char* PanelLeft    = "\xEE\x84\xAE";
inline constexpr const char* PanelRight   = "\xEE\x90\xB6";
inline constexpr const char* Pencil       = "\xEE\x87\xB9";
inline constexpr const char* Plus         = "\xEE\x85\x81";
inline constexpr const char* Pointer      = "\xEE\x87\x83";
inline constexpr const char* Redo         = "\xEE\x8A\xA0";
inline constexpr const char* Scan         = "\xEE\x89\x97";
inline constexpr const char* ScanSearch   = "\xEE\x94\xBC";
inline constexpr const char* Settings     = "\xEE\x85\x98";
inline constexpr const char* Shapes       = "\xEE\x92\xB8";
inline constexpr const char* ShieldCheck  = "\xEE\x87\xBF";
inline constexpr const char* Sparkles     = "\xEE\x90\x97";
inline constexpr const char* Trash        = "\xEE\x86\x8E";
inline constexpr const char* Undo         = "\xEE\x8A\xA1";
inline constexpr const char* Upload       = "\xEE\x86\x9E";
inline constexpr const char* Wand         = "\xEE\x8D\x9B";
inline constexpr const char* XCircle      = "\xEE\x82\x88";
inline constexpr const char* ZoomIn       = "\xEE\x86\xB6";
inline constexpr const char* ZoomOut      = "\xEE\x86\xB7";

} // namespace icon

namespace color
{

inline constexpr ImU32 Background  = IM_COL32(12, 36, 89, 255);
inline constexpr ImU32 Surface     = IM_COL32(12, 32, 82, 255);
inline constexpr ImU32 Sunken      = IM_COL32(9, 26, 66, 255);
inline constexpr ImU32 Raised      = IM_COL32(22, 52, 116, 255);
inline constexpr ImU32 Hover       = IM_COL32(34, 72, 150, 255);
inline constexpr ImU32 Border      = IM_COL32(58, 103, 191, 255);
inline constexpr ImU32 Text        = IM_COL32(255, 255, 255, 255);
inline constexpr ImU32 Muted       = IM_COL32(195, 212, 242, 255);
inline constexpr ImU32 Faint       = IM_COL32(139, 164, 210, 255);
inline constexpr ImU32 Accent      = IM_COL32(143, 188, 255, 255);
inline constexpr ImU32 AccentFill  = IM_COL32(31, 95, 196, 255);
inline constexpr ImU32 AccentSoft  = IM_COL32(31, 95, 196, 150);
inline constexpr ImU32 OnAccent    = IM_COL32(255, 255, 255, 255);
inline constexpr ImU32 Success     = IM_COL32(92, 204, 120, 255);
inline constexpr ImU32 Warning     = IM_COL32(244, 152, 60, 255);
inline constexpr ImU32 Danger      = IM_COL32(240, 92, 80, 255);
inline constexpr ImU32 Blocked     = IM_COL32(176, 160, 255, 255);
inline constexpr ImU32 TitleBand   = IM_COL32(11, 30, 87, 255);
inline constexpr ImU32 TabBandTop  = IM_COL32(30, 75, 176, 255);
inline constexpr ImU32 TabBandFoot = IM_COL32(22, 59, 146, 255);
inline constexpr ImU32 HelpBand    = IM_COL32(10, 27, 77, 255);
inline constexpr ImU32 PromptBand  = IM_COL32(8, 23, 63, 255);
inline constexpr ImU32 Panel       = IM_COL32(22, 52, 128, 235);
inline constexpr ImU32 PanelBorder = IM_COL32(67, 115, 207, 255);
inline constexpr ImU32 TabText     = IM_COL32(201, 216, 245, 255);
inline constexpr ImU32 RowFill     = IM_COL32(29, 91, 198, 255);
inline constexpr ImU32 RowBorder   = IM_COL32(134, 182, 255, 255);
inline constexpr ImU32 Highlight   = IM_COL32(255, 204, 77, 255);

} // namespace color

enum class FontRole
{
    Body,
    Strong,
    Title,
    Caption,
    Icon,
};

namespace size
{

inline constexpr float Body        = 15.0F;
inline constexpr float Caption     = 13.0F;
inline constexpr float Title       = 22.0F;
inline constexpr float Hero        = 40.0F;
inline constexpr float TileDetails = 340.0F;
inline constexpr float Gutter      = 14.0F;

} // namespace size

void ApplyTheme(float dpiScale);

[[nodiscard]] ImFont* RegularFont();
[[nodiscard]] ImFont* StrongFont();
[[nodiscard]] ImVec4  ToVec4(ImU32 color);

// Pushes a font at a role size. The base size is unscaled and DPI applies globally.
void PushFont(FontRole role);
void PopFont();

} // namespace rerevved::tools::ui
