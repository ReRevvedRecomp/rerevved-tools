#pragma once

#include "ui/ui_theme.h"

#include <imgui.h>

#include <span>
#include <string>
#include <string_view>

namespace rerevved::tools::ui
{

enum class Tone
{
    Neutral,
    Accent,
    Ok,
    Warn,
    Error,
    Blocked,
};

[[nodiscard]] ImU32       ToneColor(Tone tone);
[[nodiscard]] const char* ToneIcon(Tone tone);

// These wrap at the content edge, except SectionHeading, Tooltip and InfoMarker.
void Title(std::string_view text);
void Heading(std::string_view text);
// A rule runs to the content edge, or across width when given.
void SectionHeading(std::string_view text, float width = 0.0F);
void Body(std::string_view text);
void Muted(std::string_view text);
void Caption(std::string_view text);
void TonedText(Tone tone, std::string_view text);
void Tooltip(std::string_view text);
void InfoMarker(std::string_view text);

// Buttons return true when clicked. A disabled reason is shown on hover.
bool PrimaryButton(const char* icon, const char* label, bool enabled = true, std::string_view disabledReason = {}, float width = 0.0F);
bool SecondaryButton(const char* icon, const char* label, bool enabled = true, std::string_view disabledReason = {}, float width = 0.0F);
bool GhostButton(const char* icon, const char* label, bool enabled = true);
bool DangerButton(const char* icon, const char* label);
bool IconButton(const char* id, const char* icon, std::string_view tooltip, bool active = false, bool enabled = true);
void SameLineIfFits(const char* icon, const char* label);

void Chip(Tone tone, std::string_view label, std::string_view tooltip = {}, const char* icon = nullptr);

void                BeginCard(const char* id, float width = 0.0F, Tone accent = Tone::Neutral);
void                EndCard();
[[nodiscard]] float CardContentWidth();

bool EmptyState(const char* id, const char* icon, std::string_view title, std::string_view body, const char* actionIcon, const char* actionLabel, float height, bool dashed = false, ImVec2* rectMin = nullptr, ImVec2* rectMax = nullptr);

[[nodiscard]] ImVec2 KeycapSize(std::string_view key);
void                 DrawKeycap(ImDrawList* drawList, ImVec2 position, std::string_view key);

// Prompt bar button: an optional keycap before the label. A "##id" suffix names it for scripts.
[[nodiscard]] float PromptButtonWidth(const char* label, std::string_view key, bool primary = false);
bool                PromptButton(const char* label, std::string_view key, bool primary = false, bool enabled = true, std::string_view disabledReason = {});

// Row of text tabs. Small tabs pick pages, large ones steps or files. Badges > 0 show a count.
[[nodiscard]] float TextTabsWidth(std::span<const std::string> labels, bool small, std::span<const int> badges = {});
bool                TextTabs(const char* id, std::span<const std::string> labels, int& selected, bool small, std::span<const int> badges = {});

// Menu row: a label on the left and its value column, highlighted under the pointer.
// Items between BeginRow and EndRow form the value. They get the column's width.
void BeginRow(const char* id, std::string_view label, bool selected = false);
void EndRow();
void RowText(std::string_view value, ImU32 color = color::Text);
void ValueRow(const char* id, std::string_view label, std::string_view value);

bool Stepper(const char* id, std::span<const char* const> labels, int& selected);

bool Segmented(const char* id, std::span<const char* const> labels, int& selected, std::span<const int> badges = {});

void KeyValue(std::string_view key, std::string_view value, float keyWidth = 120.0F);
// "Key: value" line as an aligned key/value row. Indented lines continue the previous value.
void Field(std::string_view line, float keyWidth = 136.0F);
void FieldF(const char* format, ...) IM_FMTARGS(1);
bool Section(const char* label, bool defaultOpen = false);
void Divider();
void VerticalSpace(float pixels);

// Modal sheet centred on the main viewport. Call EndSheet only when BeginSheet returned true.
bool BeginSheet(const char* id, const char* icon, std::string_view title, float width, bool* open = nullptr);
void EndSheet();
// Right-aligns the next `buttons` SheetButtons, drawn with SameLine between them: the main action first, Cancel last.
void SheetFooter(int buttons);
bool SheetButton(const char* label, bool primary = false);

// Toasts raised from anywhere. Drawn once per frame by the shell.
void PushToast(Tone tone, std::string text);
void DrawToasts();
void ClearToasts();

} // namespace rerevved::tools::ui
