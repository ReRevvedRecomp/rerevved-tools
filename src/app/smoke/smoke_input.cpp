#include "smoke/smoke_input.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <format>
#include <fstream>
#include <iterator>
#include <utility>

namespace rerevved::tools
{
namespace
{

constexpr unsigned int kRetryFrames = 60;

std::string_view trim(std::string_view text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0)
    {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0)
    {
        text.remove_suffix(1);
    }
    return text;
}

// Splits words and strings in double quotes. A quoted string has no escapes.
std::expected<std::vector<std::string>, std::string> tokenize(std::string_view text)
{
    std::vector<std::string> tokens;
    while (!(text = trim(text)).empty())
    {
        if (text.front() == '"')
        {
            const auto end = text.find('"', 1);
            if (end == std::string_view::npos)
            {
                return std::unexpected("unterminated quoted string");
            }
            tokens.emplace_back(text.substr(1, end - 1));
            text.remove_prefix(end + 1);
        }
        else
        {
            const auto end = std::ranges::find_if(text, [](char value)
                                                  {
                                                      return std::isspace(static_cast<unsigned char>(value)) != 0;
                                                  });
            tokens.emplace_back(text.begin(), end);
            text.remove_prefix(static_cast<std::size_t>(std::distance(text.begin(), end)));
        }
    }
    return tokens;
}

std::optional<ImGuiKeyChord> parseChord(std::string_view text)
{
    ImGuiKeyChord chord = ImGuiKey_None;
    while (true)
    {
        const auto plus = text.find('+');
        const auto part = text.substr(0, plus);
        if (plus == std::string_view::npos)
        {
            ImGuiKey key = ImGuiKey_None;
            if (part.size() == 1 && part[0] >= 'a' && part[0] <= 'z')
            {
                key = static_cast<ImGuiKey>(ImGuiKey_A + (part[0] - 'a'));
            }
            else if (part.size() == 1 && part[0] >= '0' && part[0] <= '9')
            {
                key = static_cast<ImGuiKey>(ImGuiKey_0 + (part[0] - '0'));
            }
            else
            {
                constexpr std::array<std::pair<std::string_view, ImGuiKey>, 13> kNamed{ {
                    { "escape", ImGuiKey_Escape },
                    { "enter", ImGuiKey_Enter },
                    { "tab", ImGuiKey_Tab },
                    { "space", ImGuiKey_Space },
                    { "backspace", ImGuiKey_Backspace },
                    { "delete", ImGuiKey_Delete },
                    { "left", ImGuiKey_LeftArrow },
                    { "right", ImGuiKey_RightArrow },
                    { "up", ImGuiKey_UpArrow },
                    { "down", ImGuiKey_DownArrow },
                    { "home", ImGuiKey_Home },
                    { "end", ImGuiKey_End },
                    { "pagedown", ImGuiKey_PageDown },
                } };
                const auto                                                      named = std::ranges::find(kNamed, part, &std::pair<std::string_view, ImGuiKey>::first);
                if (named == kNamed.end())
                {
                    return std::nullopt;
                }
                key = named->second;
            }
            return chord | key;
        }
        if (part == "ctrl")
        {
            chord |= ImGuiMod_Ctrl;
        }
        else if (part == "shift")
        {
            chord |= ImGuiMod_Shift;
        }
        else if (part == "alt")
        {
            chord |= ImGuiMod_Alt;
        }
        else
        {
            return std::nullopt;
        }
        text.remove_prefix(plus + 1);
    }
}

bool matches(std::string_view label, std::string_view target)
{
    if (label == target)
    {
        return true;
    }
    const auto separator = label.find("##");
    if (separator != std::string_view::npos && label.substr(separator + 2) == target)
    {
        return true;
    }
    auto display = label.substr(0, separator);
    // Icon buttons prefix a glyph outside ASCII and spaces to their caption.
    while (!display.empty() && (static_cast<unsigned char>(display.front()) >= 0x80 || display.front() == ' '))
    {
        display.remove_prefix(1);
    }
    return display == target;
}

ImVec2 center(const ui_probe::Item& item)
{
    return { (item.min.x + item.max.x) * 0.5F, (item.min.y + item.max.y) * 0.5F };
}

} // namespace

std::expected<std::vector<SmokeInputCommand>, std::string> ParseSmokeInput(std::string_view text)
{
    using Kind = SmokeInputCommand::Kind;
    constexpr std::array<std::pair<std::string_view, Kind>, 12> kKinds{ {
        { "wait", Kind::Wait },
        { "move", Kind::Move },
        { "click", Kind::Click },
        { "key", Kind::Key },
        { "type", Kind::Type },
        { "close", Kind::Close },
        { "drop", Kind::Drop },
        { "expect", Kind::Expect },
        { "expect-missing", Kind::ExpectMissing },
        { "expect-hovered", Kind::ExpectHovered },
        { "expect-exit", Kind::ExpectExit },
        { "capture", Kind::Capture },
    } };
    std::vector<SmokeInputCommand>                              commands;
    std::size_t                                                 lineNumber = 0;
    while (!text.empty())
    {
        ++lineNumber;
        const auto newline = text.find('\n');
        auto       line    = trim(text.substr(0, newline));
        text.remove_prefix(newline == std::string_view::npos ? text.size() : newline + 1);
        if (line.empty() || line.front() == '#')
        {
            continue;
        }
        const auto failure = [&](std::string_view message)
        {
            return std::unexpected(std::format("line {}: {}", lineNumber, message));
        };
        auto tokens = tokenize(line);
        if (!tokens)
        {
            return failure(tokens.error());
        }
        const auto kind = std::ranges::find(kKinds, (*tokens)[0], &std::pair<std::string_view, Kind>::first);
        if (kind == kKinds.end())
        {
            return failure(std::format("unknown command '{}'", (*tokens)[0]));
        }
        SmokeInputCommand command;
        command.kind         = kind->second;
        command.line         = lineNumber;
        const auto arguments = tokens->size() - 1;
        switch (command.kind)
        {
            case Kind::Wait:
            {
                const auto& value       = arguments == 1 ? (*tokens)[1] : std::string();
                const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), command.count);
                if (arguments != 1 || error != std::errc{} || end != value.data() + value.size() || command.count == 0 || command.count > 600)
                {
                    return failure("wait takes a frame count from 1 to 600");
                }
                break;
            }
            case Kind::Key:
            {
                const auto chord = arguments == 1 ? parseChord((*tokens)[1]) : std::nullopt;
                if (!chord)
                {
                    return failure("key takes one chord such as ctrl+z or escape");
                }
                command.chord = *chord;
                break;
            }
            case Kind::Close:
            case Kind::ExpectExit:
                if (arguments != 0)
                {
                    return failure(std::format("{} takes no arguments", kind->first));
                }
                break;
            case Kind::Drop:
                if (arguments != 2 || (*tokens)[1].empty() || (*tokens)[2].empty())
                {
                    return failure("drop takes a target and a path");
                }
                command.target   = (*tokens)[1];
                command.argument = (*tokens)[2];
                break;
            case Kind::Capture:
                if (arguments != 1 || (*tokens)[1].empty() || !std::ranges::all_of((*tokens)[1], [](char value)
                                                                                   {
                                                                                       return std::isalnum(static_cast<unsigned char>(value)) != 0 || value == '-' || value == '_';
                                                                                   }))
                {
                    return failure("capture takes one name of letters, digits, '-' or '_'");
                }
                command.argument = (*tokens)[1];
                break;
            default:
                if (arguments != 1 || (*tokens)[1].empty())
                {
                    return failure(std::format("{} takes one quoted target", kind->first));
                }
                command.target = (*tokens)[1];
                break;
        }
        commands.push_back(std::move(command));
    }
    return commands;
}

std::expected<std::vector<SmokeInputCommand>, std::string> LoadSmokeInput(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        return std::unexpected(std::format("cannot open smoke input {}", path.string()));
    }
    const std::string text{ std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
    auto              commands = ParseSmokeInput(text);
    if (commands)
    {
        // Dropped files are named relative to the script so runs work from any directory.
        for (auto& command : *commands)
        {
            if (command.kind == SmokeInputCommand::Kind::Drop && std::filesystem::path(command.argument).is_relative())
            {
                command.argument = (path.parent_path() / command.argument).string();
            }
        }
    }
    return commands;
}

std::expected<std::size_t, std::string> FindSmokeInputItem(std::span<const ui_probe::Item> items, std::string_view target)
{
    std::size_t ordinal         = 1;
    bool        explicitOrdinal = false;
    if (const auto at = target.rfind('@'); at != std::string_view::npos)
    {
        const auto suffix = target.substr(at + 1);
        if (const auto [end, error] = std::from_chars(suffix.data(), suffix.data() + suffix.size(), ordinal); error == std::errc{} && end == suffix.data() + suffix.size() && ordinal > 0)
        {
            target          = target.substr(0, at);
            explicitOrdinal = true;
        }
        else
        {
            ordinal = 1;
        }
    }
    std::size_t found = 0;
    std::size_t index = 0;
    for (std::size_t candidate = 0; candidate < items.size(); ++candidate)
    {
        if (items[candidate].visible && matches(items[candidate].label, target) && ++found == ordinal)
        {
            index = candidate;
        }
    }
    if (found < ordinal)
    {
        return std::unexpected(std::format("'{}' matched {} visible item(s), wanted #{}", target, found, ordinal));
    }
    if (found > 1 && !explicitOrdinal)
    {
        // Ambiguous targets must pick an ordinal, so a layout change cannot silently retarget.
        return std::unexpected(
            std::format("'{}' matched {} visible items. Add @n", target, found));
    }
    return index;
}

SmokeInputRunner::SmokeInputRunner(std::vector<SmokeInputCommand> commands)
: commands(std::move(commands))
{
}

bool SmokeInputRunner::Finished() const noexcept
{
    return next == commands.size();
}

std::expected<void, std::string> SmokeInputRunner::Exited()
{
    if (next < commands.size() && commands[next].kind == SmokeInputCommand::Kind::ExpectExit)
    {
        ++next;
        if (next == commands.size())
        {
            return {};
        }
        return std::unexpected(std::format("line {}: commands remain after the application exited", commands[next].line));
    }
    return std::unexpected(next < commands.size() ? std::format("line {}: the application exited unexpectedly", commands[next].line) : std::string("the application exited after the script finished"));
}

std::expected<SmokeInputAction, std::string> SmokeInputRunner::Step(std::span<const ui_probe::Item> items, ImGuiID hoveredId)
{
    using Kind = SmokeInputCommand::Kind;
    while (next < commands.size())
    {
        const auto& command = commands[next];
        const auto  advance = [&]
        {
            ++next;
            phase = 0;
            tries = 0;
        };
        const auto failure = [&](std::string_view message) -> std::expected<SmokeInputAction, std::string>
        {
            return std::unexpected(std::format("line {}: {}", command.line, message));
        };
        const auto retry = [&](std::string_view message) -> std::expected<SmokeInputAction, std::string>
        {
            if (++tries > kRetryFrames)
            {
                return failure(message);
            }
            return SmokeInputAction{};
        };
        switch (command.kind)
        {
            case Kind::Wait:
                if (++phase >= command.count)
                {
                    advance();
                }
                return SmokeInputAction{};
            case Kind::Move:
            case Kind::Click:
            case Kind::Drop:
                if (phase == 0)
                {
                    const auto item = FindSmokeInputItem(items, command.target);
                    if (!item)
                    {
                        return retry(item.error());
                    }
                    clickPoint = center(items[*item]);
                    if (command.kind == Kind::Drop)
                    {
                        advance();
                        return SmokeInputAction{ SmokeInputDrop{ command.argument } };
                    }
                    if (command.kind == Kind::Move)
                    {
                        advance();
                    }
                    else
                    {
                        phase = 1;
                        tries = 0;
                    }
                    return SmokeInputAction{ SmokeInputMove{ clickPoint } };
                }
                if (phase == 1)
                {
                    // Press only once the pointer is over the intended item, so a miss fails the step.
                    const auto item = FindSmokeInputItem(items, command.target);
                    if (!item)
                    {
                        return retry(item.error());
                    }
                    if (items[*item].id != hoveredId)
                    {
                        return retry(std::format("'{}' is not hovered at ({:.1f},{:.1f})", command.target, clickPoint.x, clickPoint.y));
                    }
                    phase = 2;
                    return SmokeInputAction{ SmokeInputButton{ true } };
                }
                advance();
                return SmokeInputAction{ SmokeInputButton{ false } };
            case Kind::Key:
                if (phase == 0)
                {
                    phase = 1;
                    return SmokeInputAction{ SmokeInputKey{ command.chord, true } };
                }
                advance();
                return SmokeInputAction{ SmokeInputKey{ command.chord, false } };
            case Kind::Type:
                advance();
                return SmokeInputAction{ SmokeInputText{ command.target } };
            case Kind::Close:
                advance();
                return SmokeInputAction{ SmokeInputClose{} };
            case Kind::Capture:
                advance();
                return SmokeInputAction{ SmokeInputCapture{ command.argument } };
            case Kind::Expect:
            {
                const auto item = FindSmokeInputItem(items, command.target);
                if (!item)
                {
                    return retry(item.error());
                }
                advance();
                continue;
            }
            case Kind::ExpectMissing:
                if (std::ranges::any_of(items, [&](const ui_probe::Item& item)
                                        {
                                            return item.visible && matches(item.label, command.target);
                                        }))
                {
                    return retry(std::format("'{}' is still visible", command.target));
                }
                advance();
                continue;
            case Kind::ExpectHovered:
            {
                const auto item = FindSmokeInputItem(items, command.target);
                if (!item)
                {
                    return retry(item.error());
                }
                if (items[*item].id != hoveredId)
                {
                    return retry(std::format("'{}' at ({:.1f},{:.1f})-({:.1f},{:.1f}) is not hovered", command.target, items[*item].min.x, items[*item].min.y, items[*item].max.x, items[*item].max.y));
                }
                advance();
                continue;
            }
            case Kind::ExpectExit:
                return retry("the application did not exit");
        }
    }
    return SmokeInputAction{};
}

} // namespace rerevved::tools
