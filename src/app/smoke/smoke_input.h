#pragma once

#include "smoke/ui_probe.h"

#include <imgui.h>

#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace rerevved::tools
{

// One scripted smoke step. Targets name an item by its label, the text before
// "##", the id after "##", or the label without a leading icon glyph. "@n" picks
// visible match number n (starting at 1).
struct SmokeInputCommand
{
    enum class Kind
    {
        Wait,
        Move,
        Click,
        Key,
        Type,
        Close,
        Drop,
        Expect,
        ExpectMissing,
        ExpectHovered,
        ExpectExit,
        Capture,
    };

    Kind          kind = Kind::Wait;
    std::size_t   line = 0;
    std::string   target;
    std::string   argument;
    unsigned int  count = 1;
    ImGuiKeyChord chord = ImGuiKey_None;
};

[[nodiscard]] std::expected<std::vector<SmokeInputCommand>, std::string> ParseSmokeInput(std::string_view text);
[[nodiscard]] std::expected<std::vector<SmokeInputCommand>, std::string> LoadSmokeInput(const std::filesystem::path& path);

[[nodiscard]] std::expected<std::size_t, std::string> FindSmokeInputItem(std::span<const ui_probe::Item> items, std::string_view target);

struct SmokeInputMove
{
    ImVec2 logical;
};

struct SmokeInputButton
{
    bool down;
};

struct SmokeInputKey
{
    ImGuiKeyChord chord;
    bool          down;
};

struct SmokeInputText
{
    std::string text;
};

struct SmokeInputClose
{
};

struct SmokeInputDrop
{
    std::filesystem::path path;
};

struct SmokeInputCapture
{
    std::string name;
};

using SmokeInputAction = std::variant<std::monostate, SmokeInputMove, SmokeInputButton, SmokeInputKey, SmokeInputText, SmokeInputClose, SmokeInputDrop, SmokeInputCapture>;

class SmokeInputRunner
{
public:
    explicit SmokeInputRunner(std::vector<SmokeInputCommand> commands);

    [[nodiscard]] std::expected<SmokeInputAction, std::string> Step(std::span<const ui_probe::Item> items, ImGuiID hoveredId);
    // Called when the application closes. Fails unless the next command is expect-exit.
    [[nodiscard]] std::expected<void, std::string> Exited();
    [[nodiscard]] bool                             Finished() const noexcept;

private:
    std::vector<SmokeInputCommand> commands;
    std::size_t                    next  = 0;
    unsigned int                   phase = 0;
    unsigned int                   tries = 0;
    ImVec2                         clickPoint;
};

} // namespace rerevved::tools
