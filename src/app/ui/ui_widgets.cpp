#include "ui/ui_widgets.h"

#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <memory>
#include <ranges>
#include <string>
#include <vector>

namespace rerevved::tools::ui
{
namespace
{

struct Toast
{
    Tone        tone = Tone::Neutral;
    std::string text;
    double      created = -1.0;
};

std::deque<Toast>     toasts;
constexpr double      kToastSeconds     = 4.0;
constexpr std::size_t kToastSlots       = 4;
constexpr std::size_t kToastQueue       = 64;
constexpr float       kButtonHeight     = 32.0F;
constexpr float       kSheetButtonWidth = 110.0F;

ImU32 withAlpha(ImU32 color, int alpha)
{
    return (color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(alpha) << IM_COL32_A_SHIFT);
}

std::string iconLabel(const char* icon, const char* label)
{
    std::string result;
    if (icon != nullptr && *icon != '\0')
    {
        result += icon;
        if (label != nullptr && *label != '\0' && *label != '#')
        {
            result += "  ";
        }
    }
    if (label != nullptr)
    {
        result += label;
    }
    return result;
}

bool button(const char* icon, const char* label, ImU32 background, ImU32 hovered, ImU32 active, ImU32 text, bool enabled, std::string_view disabledReason, float width, float height)
{
    ImGui::PushStyleColor(ImGuiCol_Button, background);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hovered);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, active);
    ImGui::PushStyleColor(ImGuiCol_Text, text);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.0F, (height - ImGui::GetFontSize()) * 0.5F));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, (background & IM_COL32_A_MASK) == 0 ? 0.0F : 1.0F);
    ImGui::BeginDisabled(!enabled);
    const auto caption = iconLabel(icon, label);
    PushFont(FontRole::Strong);
    const bool clicked = ImGui::Button(caption.c_str(), ImVec2(width, 0.0F));
    PopFont();
    ImGui::EndDisabled();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
    if (!enabled && !disabledReason.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        Tooltip(disabledReason);
    }
    return clicked && enabled;
}

bool primaryButton(const char* icon, const char* label, bool enabled, std::string_view disabledReason, float width, float height)
{
    return button(icon, label, color::AccentFill, IM_COL32(52, 120, 224, 255), IM_COL32(24, 78, 166, 255), color::OnAccent, enabled, disabledReason, width, height);
}

void wrapped(std::string_view text, ImU32 color)
{
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    // Preserve an enclosing wrap position, or use the window edge when none is set.
    const float current = ImGui::GetCurrentWindow()->DC.TextWrapPos;
    ImGui::PushTextWrapPos(current >= 0.0F ? current : 0.0F);
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

struct CardFrame
{
    ImDrawListSplitter splitter;
    ImVec2             start;
    float              width  = 0.0F;
    Tone               accent = Tone::Neutral;
};

std::vector<std::unique_ptr<CardFrame>> cards;
constexpr ImVec2                        kCardPadding(16.0F, 14.0F);

} // namespace

ImU32 ToneColor(Tone tone)
{
    switch (tone)
    {
        case Tone::Accent:
            return color::Accent;
        case Tone::Ok:
            return color::Success;
        case Tone::Warn:
            return color::Warning;
        case Tone::Error:
            return color::Danger;
        case Tone::Blocked:
            return color::Blocked;
        case Tone::Neutral:
            break;
    }
    return color::Muted;
}

const char* ToneIcon(Tone tone)
{
    switch (tone)
    {
        case Tone::Accent:
            return icon::Info;
        case Tone::Ok:
            return icon::CheckCircle;
        case Tone::Warn:
            return icon::Alert;
        case Tone::Error:
            return icon::XCircle;
        case Tone::Blocked:
            return icon::Lock;
        case Tone::Neutral:
            break;
    }
    return icon::CircleDot;
}

void Title(std::string_view text)
{
    PushFont(FontRole::Title);
    wrapped(text, color::Text);
    PopFont();
}

void Heading(std::string_view text)
{
    PushFont(FontRole::Strong);
    wrapped(text, color::Text);
    PopFont();
}

void SectionHeading(std::string_view text, float width)
{
    PushFont(FontRole::Strong);
    const auto  start = ImGui::GetCursorScreenPos();
    const auto  size  = ImGui::CalcTextSize(text.data(), text.data() + text.size());
    const float right = start.x + (width > 0.0F ? width : cards.empty() ? ImGui::GetContentRegionAvail().x
                                                                        : CardContentWidth());
    ImGui::PushStyleColor(ImGuiCol_Text, color::Accent);
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
    ImGui::PopStyleColor();
    PopFont();
    const float lineY = start.y + size.y * 0.5F + 1.0F;
    if (start.x + size.x + 12.0F < right)
    {
        ImGui::GetWindowDrawList()->AddLine(ImVec2(start.x + size.x + 12.0F, lineY), ImVec2(right, lineY), color::Border);
    }
}

void Body(std::string_view text)
{
    wrapped(text, color::Text);
}

void Muted(std::string_view text)
{
    wrapped(text, color::Muted);
}

void Caption(std::string_view text)
{
    PushFont(FontRole::Caption);
    wrapped(text, color::Muted);
    PopFont();
}

void TonedText(Tone tone, std::string_view text)
{
    wrapped(text, ToneColor(tone));
}

void Tooltip(std::string_view text)
{
    if (text.empty())
    {
        return;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0F, 8.0F));
    if (ImGui::BeginTooltip())
    {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0F);
        ImGui::TextUnformatted(text.data(), text.data() + text.size());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    ImGui::PopStyleVar();
}

void InfoMarker(std::string_view text)
{
    ImGui::SameLine(0.0F, 6.0F);
    ImGui::PushStyleColor(ImGuiCol_Text, color::Faint);
    ImGui::TextUnformatted(icon::Info);
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered())
    {
        Tooltip(text);
    }
}

bool PrimaryButton(const char* icon, const char* label, bool enabled, std::string_view disabledReason, float width)
{
    return primaryButton(icon, label, enabled, disabledReason, width, 36.0F);
}

bool SecondaryButton(const char* icon, const char* label, bool enabled, std::string_view disabledReason, float width)
{
    return button(icon, label, color::Raised, color::Hover, IM_COL32(50, 88, 156, 255), color::Text, enabled, disabledReason, width, kButtonHeight);
}

bool GhostButton(const char* icon, const char* label, bool enabled)
{
    return button(icon, label, IM_COL32(0, 0, 0, 0), color::Raised, color::Hover, color::Muted, enabled, {}, 0.0F, kButtonHeight);
}

bool DangerButton(const char* icon, const char* label)
{
    return button(icon, label, IM_COL32(229, 83, 75, 36), IM_COL32(229, 83, 75, 70), IM_COL32(229, 83, 75, 100), IM_COL32(255, 160, 154, 255), true, {}, 0.0F, kButtonHeight);
}

bool IconButton(const char* id, const char* icon, std::string_view tooltip, bool active, bool enabled)
{
    ImGui::PushID(id);
    ImGui::PushStyleColor(ImGuiCol_Button, active ? color::AccentSoft : IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color::Hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(50, 88, 156, 255));
    ImGui::PushStyleColor(ImGuiCol_Text, active ? color::Accent : color::Muted);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0F);
    ImGui::BeginDisabled(!enabled);
    // The "##id" suffix names the item for scripted smoke input without changing the glyph.
    const bool clicked = ImGui::Button((std::string(icon) + "##" + id).c_str(), ImVec2(kButtonHeight, kButtonHeight));
    ImGui::EndDisabled();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
    {
        Tooltip(tooltip);
    }
    ImGui::PopID();
    return clicked && enabled;
}

void SameLineIfFits(const char* icon, const char* label)
{
    const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    ImGui::SameLine();
    PushFont(FontRole::Strong);
    const float width = ImGui::CalcTextSize(iconLabel(icon, label).c_str()).x + 28.0F;
    PopFont();
    if (ImGui::GetCursorScreenPos().x + width > right)
    {
        ImGui::NewLine();
    }
}

void Chip(Tone tone, std::string_view label, std::string_view tooltip, const char* icon)
{
    const auto  foreground = ToneColor(tone);
    const auto* glyph      = icon != nullptr ? icon : ToneIcon(tone);
    std::string text       = std::string(glyph) + " " + std::string(label);
    PushFont(FontRole::Caption);
    const auto textSize = ImGui::CalcTextSize(text.c_str());
    const auto padding  = ImVec2(8.0F, 3.0F);
    const auto size     = ImVec2(textSize.x + padding.x * 2.0F, textSize.y + padding.y * 2.0F);
    const auto position = ImGui::GetCursorScreenPos();
    auto*      drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(position, ImVec2(position.x + size.x, position.y + size.y), withAlpha(foreground, tone == Tone::Neutral ? 26 : 34), size.y * 0.5F);
    drawList->AddText(ImVec2(position.x + padding.x, position.y + padding.y), tone == Tone::Neutral ? color::Text : foreground, text.c_str());
    PopFont();
    ImGui::Dummy(size);
    if (!tooltip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    {
        Tooltip(tooltip);
    }
}

void BeginCard(const char* id, float width, Tone accent)
{
    // A splitter draws the background after the content is measured, so cards
    // render on their first frame, unlike child windows that resize automatically.
    auto frame    = std::make_unique<CardFrame>();
    frame->start  = ImGui::GetCursorScreenPos();
    frame->width  = width > 0.0F ? width : ImGui::GetContentRegionAvail().x;
    frame->accent = accent;
    frame->splitter.Split(ImGui::GetWindowDrawList(), 2);
    frame->splitter.SetCurrentChannel(ImGui::GetWindowDrawList(), 1);
    ImGui::PushID(id);
    ImGui::SetCursorScreenPos(ImVec2(frame->start.x + kCardPadding.x, frame->start.y + kCardPadding.y));
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + frame->width - kCardPadding.x * 2.0F);
    cards.push_back(std::move(frame));
}

void EndCard()
{
    IM_ASSERT(!cards.empty());
    auto frame = std::move(cards.back());
    cards.pop_back();
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    ImGui::PopID();
    auto*      drawList = ImGui::GetWindowDrawList();
    const auto minimum  = frame->start;
    const auto maximum  = ImVec2(frame->start.x + frame->width, ImGui::GetItemRectMax().y + kCardPadding.y);
    frame->splitter.SetCurrentChannel(drawList, 0);
    drawList->AddRectFilled(minimum, maximum, color::Sunken);
    drawList->AddRect(minimum, maximum, frame->accent == Tone::Neutral ? color::Border : withAlpha(ToneColor(frame->accent), 160));
    frame->splitter.Merge(drawList);
    ImGui::SetCursorScreenPos(minimum);
    ImGui::Dummy(ImVec2(frame->width, maximum.y - minimum.y));
}

float CardContentWidth()
{
    IM_ASSERT(!cards.empty());
    return cards.back()->width - kCardPadding.x * 2.0F;
}

bool EmptyState(const char* id, const char* icon, std::string_view title, std::string_view body, const char* actionIcon, const char* actionLabel, float height, bool dashed, ImVec2* rectMin, ImVec2* rectMax)
{
    ImGui::PushID(id);
    const float width    = ImGui::GetContentRegionAvail().x;
    const auto  minimum  = ImGui::GetCursorScreenPos();
    const auto  maximum  = ImVec2(minimum.x + width, minimum.y + height);
    auto*       drawList = ImGui::GetWindowDrawList();
    const bool  hovered  = ImGui::IsMouseHoveringRect(minimum, maximum) && ImGui::IsWindowHovered();
    drawList->AddRectFilled(minimum, maximum, hovered && dashed ? withAlpha(color::Accent, 14) : color::Sunken);
    if (dashed)
    {
        const ImU32 outline = hovered ? color::Accent : color::Border;
        const float dash    = 8.0F;
        const auto  edge    = [&](ImVec2 from, ImVec2 to)
        {
            const float length = std::hypot(to.x - from.x, to.y - from.y);
            const auto  steps  = static_cast<int>(length / (dash * 2.0F));
            for (int step = 0; step <= steps; ++step)
            {
                const float start = static_cast<float>(step) * dash * 2.0F / length;
                const float end   = std::min(1.0F, start + dash / length);
                drawList->AddLine(ImVec2(from.x + (to.x - from.x) * start, from.y + (to.y - from.y) * start), ImVec2(from.x + (to.x - from.x) * end, from.y + (to.y - from.y) * end), outline, 1.5F);
            }
        };
        const float inset = 1.0F;
        edge(ImVec2(minimum.x + 12, minimum.y + inset), ImVec2(maximum.x - 12, minimum.y + inset));
        edge(ImVec2(minimum.x + 12, maximum.y - inset), ImVec2(maximum.x - 12, maximum.y - inset));
        edge(ImVec2(minimum.x + inset, minimum.y + 12), ImVec2(minimum.x + inset, maximum.y - 12));
        edge(ImVec2(maximum.x - inset, minimum.y + 12), ImVec2(maximum.x - inset, maximum.y - 12));
    }
    else
    {
        drawList->AddRect(minimum, maximum, color::Border);
    }
    if (rectMin != nullptr)
    {
        *rectMin = minimum;
    }
    if (rectMax != nullptr)
    {
        *rectMax = maximum;
    }

    PushFont(FontRole::Icon);
    const auto iconSize = ImGui::CalcTextSize(icon);
    PopFont();
    PushFont(FontRole::Strong);
    const auto titleSize = ImGui::CalcTextSize(title.data(), title.data() + title.size());
    PopFont();
    const float wrap     = std::min(width - 48.0F, ImGui::GetFontSize() * 30.0F);
    const auto  bodySize = ImGui::CalcTextSize(body.data(), body.data() + body.size(), false, wrap);
    const float action   = actionLabel != nullptr ? 36.0F + 16.0F : 0.0F;
    const float text     = titleSize.y + 6.0F + bodySize.y + action;
    const bool  showIcon = height >= iconSize.y + 12.0F + text + 32.0F;
    const float total    = (showIcon ? iconSize.y + 12.0F : 0.0F) + text;
    float       y        = minimum.y + std::max(16.0F, (height - total) * 0.5F);

    if (showIcon)
    {
        PushFont(FontRole::Icon);
        drawList->AddText(ImVec2(minimum.x + (width - iconSize.x) * 0.5F, y), hovered && dashed ? color::Accent : color::Faint, icon);
        PopFont();
        y += iconSize.y + 12.0F;
    }
    PushFont(FontRole::Strong);
    drawList->AddText(ImVec2(minimum.x + (width - titleSize.x) * 0.5F, y), color::Text, title.data(), title.data() + title.size());
    PopFont();
    y += titleSize.y + 6.0F;
    ImGui::SetCursorScreenPos(ImVec2(minimum.x + (width - bodySize.x) * 0.5F, y));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap);
    ImGui::PushStyleColor(ImGuiCol_Text, color::Muted);
    ImGui::TextUnformatted(body.data(), body.data() + body.size());
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();
    y += bodySize.y + 16.0F;
    bool clicked = false;
    if (actionLabel != nullptr)
    {
        const auto caption = iconLabel(actionIcon, actionLabel);
        PushFont(FontRole::Strong);
        const float buttonWidth = ImGui::CalcTextSize(caption.c_str()).x + 28.0F;
        PopFont();
        ImGui::SetCursorScreenPos(ImVec2(minimum.x + (width - buttonWidth) * 0.5F, y));
        clicked = PrimaryButton(actionIcon, actionLabel, true, {}, buttonWidth);
    }
    ImGui::SetCursorScreenPos(minimum);
    ImGui::Dummy(ImVec2(width, height));
    ImGui::PopID();
    return clicked;
}

namespace
{

constexpr float kKeycapFont   = 12.0F;
constexpr float kPromptHeight = 40.0F;
constexpr float kRowHeight    = 40.0F;
constexpr float kRowPadding   = 12.0F;
// The value column starts this far across, as in the game's option menus.
constexpr float kRowSplit = 0.42F;

struct RowFrame
{
    ImVec2  start;
    float   width    = 0.0F;
    ImGuiID heightId = 0;
    ImGuiID hoverId  = 0;
};

std::vector<RowFrame> rows;

std::string_view shownText(std::string_view label)
{
    return label.substr(0, label.find("##"));
}

struct TabMetrics
{
    float    padding = 0.0F;
    float    height  = 0.0F;
    FontRole font    = FontRole::Body;
};

TabMetrics tabMetrics(bool small)
{
    return small ? TabMetrics{ 14.0F, 25.0F, FontRole::Caption } : TabMetrics{ 16.0F, 31.0F, FontRole::Body };
}

float badgeWidth(int count)
{
    PushFont(FontRole::Caption);
    const float width = ImGui::CalcTextSize(std::to_string(count).c_str()).x + 12.0F;
    PopFont();
    return width;
}

void arrow(ImDrawList* drawList, ImVec2 center, bool left, ImU32 fill)
{
    const float x = left ? -5.0F : 5.0F;
    drawList->AddTriangleFilled(ImVec2(center.x - x, center.y - 7.0F), ImVec2(center.x + x, center.y), ImVec2(center.x - x, center.y + 7.0F), fill);
}

} // namespace

ImVec2 KeycapSize(std::string_view key)
{
    ImGui::PushFont(StrongFont(), kKeycapFont);
    const auto text = ImGui::CalcTextSize(key.data(), key.data() + key.size());
    ImGui::PopFont();
    return { text.x + 14.0F, text.y + 7.0F };
}

void DrawKeycap(ImDrawList* drawList, ImVec2 position, std::string_view key)
{
    const auto size    = KeycapSize(key);
    const auto maximum = ImVec2(position.x + size.x, position.y + size.y);
    drawList->AddRectFilled(position, maximum, IM_COL32(154, 164, 184, 255), 5.0F);
    drawList->AddRectFilled(position, ImVec2(maximum.x, maximum.y - 2.0F), IM_COL32(223, 228, 238, 255), 5.0F);
    ImGui::PushFont(StrongFont(), kKeycapFont);
    drawList->AddText(ImVec2(position.x + 7.0F, position.y + 3.0F), IM_COL32(26, 36, 64, 255), key.data(), key.data() + key.size());
    ImGui::PopFont();
}

float PromptButtonWidth(const char* label, std::string_view key, bool primary)
{
    const auto text = shownText(label);
    PushFont(primary ? FontRole::Strong : FontRole::Body);
    const float width = ImGui::CalcTextSize(text.data(), text.data() + text.size()).x;
    PopFont();
    return (key.empty() ? 18.0F : 16.0F + KeycapSize(key).x) + width + 18.0F;
}

bool PromptButton(const char* label, std::string_view key, bool primary, bool enabled, std::string_view disabledReason)
{
    const float width   = PromptButtonWidth(label, key, primary);
    const auto  minimum = ImGui::GetCursorScreenPos();
    const auto  maximum = ImVec2(minimum.x + width, minimum.y + kPromptHeight);
    ImGui::BeginDisabled(!enabled);
    const bool clicked = ImGui::InvisibleButton(label, ImVec2(width, kPromptHeight), ImGuiButtonFlags_EnableNav);
    ImGui::EndDisabled();
    const bool hovered = enabled && ImGui::IsItemHovered();
    const bool held    = enabled && ImGui::IsItemActive();
    ImU32      fill    = primary ? IM_COL32(36, 96, 204, 255) : IM_COL32(20, 54, 136, 255);
    if (held)
    {
        fill = primary ? IM_COL32(28, 82, 176, 255) : IM_COL32(16, 44, 112, 255);
    }
    else if (hovered)
    {
        fill = primary ? IM_COL32(52, 118, 224, 255) : IM_COL32(34, 78, 170, 255);
    }
    const ImU32 border   = primary ? IM_COL32(156, 195, 255, 255) : IM_COL32(75, 123, 209, 255);
    const int   alpha    = enabled ? 255 : 110;
    auto*       drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(minimum, maximum, withAlpha(fill, alpha), 18.0F, ImDrawFlags_RoundCornersRight);
    drawList->AddRect(minimum, maximum, withAlpha(border, alpha), 18.0F, 1.0F, ImDrawFlags_RoundCornersRight);
    float x = minimum.x + (key.empty() ? 18.0F : 6.0F);
    if (!key.empty())
    {
        const auto cap = KeycapSize(key);
        DrawKeycap(drawList, ImVec2(x, minimum.y + (kPromptHeight - cap.y) * 0.5F), key);
        x += cap.x + 10.0F;
    }
    const auto text = shownText(label);
    PushFont(primary ? FontRole::Strong : FontRole::Body);
    drawList->AddText(ImVec2(x, minimum.y + (kPromptHeight - ImGui::GetFontSize()) * 0.5F), enabled ? color::Text : color::Faint, text.data(), text.data() + text.size());
    PopFont();
    if (!enabled && !disabledReason.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        Tooltip(disabledReason);
    }
    return clicked && enabled;
}

float TextTabsWidth(std::span<const std::string> labels, bool small, std::span<const int> badges)
{
    const auto metrics = tabMetrics(small);
    float      width   = 0.0F;
    PushFont(metrics.font);
    for (std::size_t index = 0; index < labels.size(); ++index)
    {
        const auto text = shownText(labels[index]);
        width += ImGui::CalcTextSize(text.data(), text.data() + text.size()).x + metrics.padding * 2.0F + 4.0F;
        if (index < badges.size() && badges[index] > 0)
        {
            width += badgeWidth(badges[index]) + 8.0F;
        }
    }
    PopFont();
    return std::max(0.0F, width - 4.0F);
}

bool TextTabs(const char* id, std::span<const std::string> labels, int& selected, bool small, std::span<const int> badges)
{
    ImGui::PushID(id);
    const auto metrics  = tabMetrics(small);
    const auto origin   = ImGui::GetCursorScreenPos();
    auto*      drawList = ImGui::GetWindowDrawList();
    bool       changed  = false;
    float      x        = origin.x;
    PushFont(metrics.font);
    for (std::size_t index = 0; index < labels.size(); ++index)
    {
        const auto  text    = shownText(labels[index]);
        const float textW   = ImGui::CalcTextSize(text.data(), text.data() + text.size()).x;
        const int   badge   = index < badges.size() ? badges[index] : 0;
        const float extra   = badge > 0 ? badgeWidth(badge) + 8.0F : 0.0F;
        const auto  bounds  = ImVec2(textW + metrics.padding * 2.0F + extra, metrics.height);
        const auto  minimum = ImVec2(x, origin.y);
        const auto  maximum = ImVec2(x + bounds.x, origin.y + bounds.y);
        ImGui::SetCursorScreenPos(minimum);
        ImGui::PushID(static_cast<int>(index));
        // Tabs stay out of navigation with the Tab key, which cannot reach a clipped one.
        if (ImGui::InvisibleButton(labels[index].c_str(), bounds))
        {
            changed  = selected != static_cast<int>(index);
            selected = static_cast<int>(index);
        }
        ImGui::PopID();
        const bool active  = selected == static_cast<int>(index);
        const bool hovered = ImGui::IsItemHovered();
        if (active)
        {
            drawList->AddRectFilled(minimum, maximum, color::AccentFill);
            drawList->AddRect(minimum, maximum, IM_COL32(127, 178, 255, 255));
        }
        else if (hovered)
        {
            drawList->AddRectFilled(minimum, maximum, IM_COL32(255, 255, 255, 18));
        }
        const float textY = origin.y + (bounds.y - ImGui::GetFontSize()) * 0.5F;
        drawList->AddText(ImVec2(x + metrics.padding, textY), active || hovered ? color::Text : color::TabText, text.data(), text.data() + text.size());
        if (badge > 0)
        {
            const auto count = std::to_string(badge);
            PushFont(FontRole::Caption);
            const float height = ImGui::GetFontSize() + 4.0F;
            const auto  pill   = ImVec2(x + metrics.padding + textW + 8.0F, origin.y + (bounds.y - height) * 0.5F);
            drawList->AddRectFilled(pill, ImVec2(pill.x + extra - 8.0F, pill.y + height), color::Highlight, height * 0.5F);
            drawList->AddText(ImVec2(pill.x + 6.0F, pill.y + 2.0F), IM_COL32(26, 36, 64, 255), count.c_str());
            PopFont();
        }
        x += bounds.x + 4.0F;
    }
    PopFont();
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(std::max(0.0F, x - origin.x - 4.0F), metrics.height));
    ImGui::PopID();
    return changed;
}

void BeginRow(const char* id, std::string_view label, bool selected)
{
    ImGui::PushID(id);
    RowFrame frame;
    frame.start          = ImGui::GetCursorScreenPos();
    frame.width          = ImGui::GetContentRegionAvail().x;
    frame.heightId       = ImGui::GetID("##row-height");
    frame.hoverId        = ImGui::GetID("##row-hover");
    auto*       storage  = ImGui::GetStateStorage();
    const float height   = storage->GetFloat(frame.heightId, kRowHeight);
    const auto  maximum  = ImVec2(frame.start.x + frame.width, frame.start.y + height);
    auto*       drawList = ImGui::GetWindowDrawList();
    // Last frame's height and hover place the highlight under this frame's items.
    if (selected || storage->GetBool(frame.hoverId))
    {
        drawList->AddRectFilled(frame.start, maximum, color::RowFill);
        drawList->AddRect(frame.start, maximum, color::RowBorder);
    }
    const float split = frame.start.x + std::floor(frame.width * kRowSplit);
    drawList->PushClipRect(frame.start, ImVec2(split - 8.0F, maximum.y), true);
    drawList->AddText(ImVec2(frame.start.x + kRowPadding, frame.start.y + (height - ImGui::GetFontSize()) * 0.5F), color::Text, label.data(), label.data() + label.size());
    drawList->PopClipRect();
    ImGui::SetCursorScreenPos(ImVec2(split, frame.start.y + (kRowHeight - ImGui::GetFrameHeight()) * 0.5F));
    ImGui::BeginGroup();
    const float valueWidth = frame.start.x + frame.width - kRowPadding - split;
    ImGui::PushItemWidth(valueWidth);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + valueWidth);
    rows.push_back(frame);
}

void EndRow()
{
    IM_ASSERT(!rows.empty());
    const auto frame = rows.back();
    rows.pop_back();
    ImGui::PopTextWrapPos();
    ImGui::PopItemWidth();
    ImGui::EndGroup();
    const float bottom  = ImGui::GetItemRectMax().y + (kRowHeight - ImGui::GetFrameHeight()) * 0.5F;
    const float height  = std::max(kRowHeight, bottom - frame.start.y);
    const auto  maximum = ImVec2(frame.start.x + frame.width, frame.start.y + height);
    auto*       storage = ImGui::GetStateStorage();
    storage->SetFloat(frame.heightId, height);
    storage->SetBool(frame.hoverId, ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && ImGui::IsMouseHoveringRect(frame.start, maximum));
    // Rows sit 2 px apart, like the game's menu lists.
    ImGui::SetCursorScreenPos(frame.start);
    ImGui::Dummy(ImVec2(frame.width, height - ImGui::GetStyle().ItemSpacing.y + 2.0F));
    ImGui::PopID();
}

void RowText(std::string_view value, ImU32 color)
{
    const float width     = ImGui::CalcItemWidth();
    const float textWidth = ImGui::CalcTextSize(value.data(), value.data() + value.size()).x;
    ImGui::AlignTextToFramePadding();
    if (textWidth < width)
    {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (width - textWidth) * 0.5F);
    }
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextUnformatted(value.data(), value.data() + value.size());
    ImGui::PopStyleColor();
}

void ValueRow(const char* id, std::string_view label, std::string_view value)
{
    BeginRow(id, label);
    RowText(value, IM_COL32(219, 230, 250, 255));
    EndRow();
}

bool Stepper(const char* id, std::span<const char* const> labels, int& selected)
{
    const float width    = ImGui::CalcItemWidth();
    const float height   = ImGui::GetFrameHeight();
    const auto  origin   = ImGui::GetCursorScreenPos();
    const bool  disabled = (ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled) != 0;
    const int   count    = static_cast<int>(labels.size());
    const bool  canLess  = selected > 0;
    const bool  canMore  = selected + 1 < count;
    bool        changed  = false;
    // The hidden names, such as "land-size-less", address the arrows in scripts.
    ImGui::SetCursorScreenPos(origin);
    if (ImGui::InvisibleButton((std::string("##") + id + "-less").c_str(), ImVec2(height, height), ImGuiButtonFlags_EnableNav) && canLess)
    {
        --selected;
        changed = true;
    }
    const bool lessHovered = ImGui::IsItemHovered();
    ImGui::SetCursorScreenPos(ImVec2(origin.x + width - height, origin.y));
    if (ImGui::InvisibleButton((std::string("##") + id + "-more").c_str(), ImVec2(height, height), ImGuiButtonFlags_EnableNav) && canMore)
    {
        ++selected;
        changed = true;
    }
    const bool moreHovered = ImGui::IsItemHovered();
    const auto fill        = [&](bool can, bool hovered)
    {
        if (disabled || !can)
        {
            return IM_COL32(127, 169, 232, 90);
        }
        return hovered ? IM_COL32(255, 228, 130, 255) : IM_COL32(255, 210, 63, 255);
    };
    auto* drawList = ImGui::GetWindowDrawList();
    arrow(drawList, ImVec2(origin.x + height * 0.5F, origin.y + height * 0.5F), true, fill(canLess, lessHovered));
    arrow(drawList, ImVec2(origin.x + width - height * 0.5F, origin.y + height * 0.5F), false, fill(canMore, moreHovered));
    if (selected >= 0 && selected < count)
    {
        const char* text      = labels[static_cast<std::size_t>(selected)];
        const float textWidth = ImGui::CalcTextSize(text).x;
        drawList->AddText(ImVec2(origin.x + (width - textWidth) * 0.5F, origin.y + (height - ImGui::GetFontSize()) * 0.5F), disabled ? color::Faint : color::Text, text);
    }
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(width, height));
    return changed;
}

bool Segmented(const char* id, std::span<const char* const> labels, int& selected, std::span<const int> badges)
{
    ImGui::PushID(id);
    bool               changed = false;
    const auto         origin  = ImGui::GetCursorScreenPos();
    const float        height  = 34.0F;
    float              width   = 8.0F;
    std::vector<float> widths;
    PushFont(FontRole::Strong);
    for (std::size_t index = 0; index < labels.size(); ++index)
    {
        float itemWidth = ImGui::CalcTextSize(labels[index]).x + 32.0F;
        if (index < badges.size() && badges[index] > 0)
        {
            itemWidth += 26.0F;
        }
        widths.push_back(itemWidth);
        width += itemWidth;
    }
    PopFont();
    auto* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), color::Sunken, 4.0F);
    drawList->AddRect(origin, ImVec2(origin.x + width, origin.y + height), color::Border, 4.0F);
    float x = origin.x + 4.0F;
    for (std::size_t index = 0; index < labels.size(); ++index)
    {
        ImGui::PushID(static_cast<int>(index));
        const auto minimum = ImVec2(x, origin.y + 4.0F);
        const auto maximum = ImVec2(x + widths[index], origin.y + height - 4.0F);
        ImGui::SetCursorScreenPos(minimum);
        // The hidden label names the segment for scripted input.
        if (ImGui::InvisibleButton((std::string("##") + labels[index]).c_str(), ImVec2(widths[index], height - 8.0F)))
        {
            changed  = selected != static_cast<int>(index);
            selected = static_cast<int>(index);
        }
        const bool active  = selected == static_cast<int>(index);
        const bool hovered = ImGui::IsItemHovered();
        if (active || hovered)
        {
            drawList->AddRectFilled(minimum, maximum, active ? color::Raised : IM_COL32(255, 255, 255, 8), 3.0F);
        }
        PushFont(FontRole::Strong);
        const float textY = minimum.y + (maximum.y - minimum.y - ImGui::GetFontSize()) * 0.5F;
        drawList->AddText(ImVec2(minimum.x + 16.0F, textY), active ? color::Accent : color::Muted, labels[index]);
        const float labelWidth = ImGui::CalcTextSize(labels[index]).x;
        PopFont();
        if (index < badges.size() && badges[index] > 0)
        {
            const auto text = std::to_string(badges[index]);
            PushFont(FontRole::Caption);
            const auto  textSize = ImGui::CalcTextSize(text.c_str());
            const float left     = minimum.x + 16.0F + labelWidth + 8.0F;
            const float centerY  = (minimum.y + maximum.y) * 0.5F;
            drawList->AddRectFilled(ImVec2(left, centerY - textSize.y * 0.5F - 2.0F), ImVec2(left + textSize.x + 10.0F, centerY + textSize.y * 0.5F + 2.0F), withAlpha(color::Warning, 50), textSize.y);
            drawList->AddText(ImVec2(left + 5.0F, centerY - textSize.y * 0.5F), color::Warning, text.c_str());
            PopFont();
        }
        x += widths[index];
        ImGui::PopID();
    }
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(width, height));
    ImGui::PopID();
    return changed;
}

void KeyValue(std::string_view key, std::string_view value, float keyWidth)
{
    const float start = ImGui::GetCursorPosX();
    ImGui::PushStyleColor(ImGuiCol_Text, color::Muted);
    ImGui::TextUnformatted(key.data(), key.data() + key.size());
    ImGui::PopStyleColor();
    if (ImGui::CalcTextSize(key.data(), key.data() + key.size()).x + 8.0F <= keyWidth)
    {
        ImGui::SameLine(start + keyWidth);
    }
    else
    {
        ImGui::SetCursorPosX(start + keyWidth);
    }
    ImGui::PushTextWrapPos(0.0F);
    ImGui::TextUnformatted(value.data(), value.data() + value.size());
    ImGui::PopTextWrapPos();
}

void Field(std::string_view line, float keyWidth)
{
    if (line.starts_with(' '))
    {
        const auto first = line.find_first_not_of(' ');
        KeyValue({}, first == std::string_view::npos ? std::string_view{} : line.substr(first), keyWidth);
        return;
    }
    const auto split = line.find(": ");
    if (split == std::string_view::npos || split > 48)
    {
        Body(line);
        return;
    }
    KeyValue(line.substr(0, split), line.substr(split + 2), keyWidth);
}

void FieldF(const char* format, ...)
{
    std::array<char, 256> buffer{};
    va_list               arguments;
    va_start(arguments, format);
    const auto written = std::vsnprintf(buffer.data(), buffer.size(), format, arguments);
    va_end(arguments);
    Field(std::string_view(buffer.data(), static_cast<std::size_t>(std::clamp(written, 0, static_cast<int>(buffer.size()) - 1))));
}

bool Section(const char* label, bool defaultOpen)
{
    ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, withAlpha(color::Text, 14));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, withAlpha(color::Text, 22));
    PushFont(FontRole::Strong);
    const bool open = ImGui::CollapsingHeader(label, defaultOpen ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None);
    PopFont();
    ImGui::PopStyleColor(3);
    return open;
}

void Divider()
{
    VerticalSpace(4.0F);
    const auto  position = ImGui::GetCursorScreenPos();
    const float width    = ImGui::GetContentRegionAvail().x;
    ImGui::GetWindowDrawList()->AddLine(position, ImVec2(position.x + width, position.y), color::Border);
    VerticalSpace(5.0F);
}

void VerticalSpace(float pixels)
{
    ImGui::Dummy(ImVec2(0.0F, pixels));
}

bool BeginSheet(const char* id, const char* icon, std::string_view title, float width, bool* open)
{
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
    ImGui::SetNextWindowSize(ImVec2(width, 0.0F), ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0F), ImVec2(width, viewport->WorkSize.y * 0.9F));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0F, 20.0F));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0F);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, color::Surface);
    const bool visible = ImGui::BeginPopupModal(id, open, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
    if (!visible)
    {
        return false;
    }
    PushFont(FontRole::Title);
    ImGui::PushStyleColor(ImGuiCol_Text, color::Accent);
    ImGui::TextUnformatted(icon);
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0F, 10.0F);
    ImGui::TextUnformatted(title.data(), title.data() + title.size());
    PopFont();
    VerticalSpace(4.0F);
    return true;
}

void SheetFooter(int buttons)
{
    VerticalSpace(8.0F);
    Divider();
    VerticalSpace(4.0F);
    const float total = static_cast<float>(buttons) * kSheetButtonWidth + static_cast<float>(buttons - 1) * ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - total);
}

bool SheetButton(const char* label, bool primary)
{
    return primary ? primaryButton(nullptr, label, true, {}, kSheetButtonWidth, kButtonHeight) : SecondaryButton(nullptr, label, true, {}, kSheetButtonWidth);
}

void EndSheet()
{
    ImGui::EndPopup();
}

void PushToast(Tone tone, std::string text)
{
    toasts.push_back(Toast{ tone, std::move(text), -1.0 });
    // Undisplayed notices wait for a free slot. Only a runaway batch drops the oldest.
    while (toasts.size() > kToastQueue)
    {
        toasts.pop_front();
    }
}

void ClearToasts()
{
    toasts.clear();
}

void DrawToasts()
{
    const double now = ImGui::GetTime();
    std::erase_if(toasts, [now](const Toast& toast)
                  {
                      return toast.created >= 0.0 && now - toast.created > kToastSeconds;
                  });
    const auto* viewport = ImGui::GetMainViewport();
    float       y        = viewport->WorkPos.y + size::Gutter + 12.0F;
    const float width    = 360.0F;
    auto*       drawList = ImGui::GetForegroundDrawList(const_cast<ImGuiViewport*>(viewport));
    for (auto& toast : toasts | std::views::take(kToastSlots))
    {
        if (toast.created < 0.0)
        {
            toast.created = now;
        }
        const float age   = static_cast<float>(now - toast.created);
        const float alpha = std::clamp(std::min(age * 6.0F, (static_cast<float>(kToastSeconds) - age) * 3.0F), 0.0F, 1.0F);
        const float wrap  = width - 56.0F;
        const auto  text  = ImGui::CalcTextSize(toast.text.c_str(), nullptr, false, wrap);
        const float h     = std::max(44.0F, text.y + 24.0F);
        const auto  min   = ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - width - 16.0F, y);
        const auto  max   = ImVec2(min.x + width, y + h);
        const auto  a     = static_cast<int>(alpha * 255.0F);
        drawList->AddRectFilled(ImVec2(min.x, min.y + 3.0F), ImVec2(max.x, max.y + 3.0F), IM_COL32(0, 0, 0, a / 3), 10.0F);
        drawList->AddRectFilled(min, max, withAlpha(color::Raised, a), 10.0F);
        drawList->AddRect(min, max, withAlpha(color::Border, a), 10.0F);
        drawList->AddRectFilled(ImVec2(min.x, min.y + 8.0F), ImVec2(min.x + 3.0F, max.y - 8.0F), withAlpha(ToneColor(toast.tone), a), 2.0F);
        drawList->AddText(ImVec2(min.x + 16.0F, min.y + (h - ImGui::GetFontSize()) * 0.5F), withAlpha(ToneColor(toast.tone), a), ToneIcon(toast.tone));
        drawList->AddText(nullptr, 0.0F, ImVec2(min.x + 42.0F, min.y + 12.0F), withAlpha(color::Text, a), toast.text.c_str(), nullptr, wrap);
        y += h + 8.0F;
    }
}

} // namespace rerevved::tools::ui
