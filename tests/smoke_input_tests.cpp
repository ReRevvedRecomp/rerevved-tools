#include "smoke/smoke_input.h"

#include <expected>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace
{

using namespace rerevved::tools;
using Kind = SmokeInputCommand::Kind;

int failures = 0;

void expect(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

ui_probe::Item item(ImGuiID id, std::string label, ImVec2 min, ImVec2 max, bool visible = true)
{
    return ui_probe::Item{ id, std::move(label), min, max, visible };
}

void testParse()
{
    const auto parsed = ParseSmokeInput("# comment\n\nwait 3\nclick \"Choose file\"\nkey ctrl+shift+z\ndrop \"Map\" \"a b.png\"\ncapture shot-1\nexpect-exit\n");
    expect(parsed.has_value(), "parse a valid script");
    if (parsed)
    {
        expect(parsed->size() == 6, "skip comments and blank lines");
        expect((*parsed)[0].kind == Kind::Wait && (*parsed)[0].count == 3 && (*parsed)[0].line == 3, "wait keeps its count and line");
        expect((*parsed)[1].kind == Kind::Click && (*parsed)[1].target == "Choose file", "quoted targets keep spaces");
        expect((*parsed)[2].chord == (ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z), "key parses modifiers");
        expect((*parsed)[3].target == "Map" && (*parsed)[3].argument == "a b.png", "drop takes a target and a path");
        expect((*parsed)[4].argument == "shot-1", "capture keeps its name");
    }
    for (const std::string_view bad : { "wait 0", "wait 601", "wait x", "key ctrl+", "key meta+z", "click", "click \"open", "capture ../x", "close now", "jump \"x\"" })
    {
        const auto failed = ParseSmokeInput(bad);
        expect(!failed && failed.error().starts_with("line 1: "), std::string("reject ") + std::string(bad));
    }
}

void testFind()
{
    const std::vector items{
        item(1, "Choose file", { 0, 0 }, { 10, 10 }),
        item(2, "\xEE\x80\x81  Open##open", { 0, 20 }, { 10, 30 }),
        item(3, "Save##first", { 0, 40 }, { 10, 50 }),
        item(4, "Save##second", { 0, 60 }, { 10, 70 }),
        item(5, "Hidden", { 0, 80 }, { 10, 90 }, false),
    };
    expect(FindSmokeInputItem(items, "Choose file") == 0U, "match a full label");
    expect(FindSmokeInputItem(items, "Open") == 1U, "match a caption without its glyph");
    expect(FindSmokeInputItem(items, "open") == 1U, "match the id after ##");
    expect(FindSmokeInputItem(items, "Save##second") == 3U, "match a full label with an id");
    expect(!FindSmokeInputItem(items, "Save"), "reject an ambiguous target");
    expect(FindSmokeInputItem(items, "Save@2") == 3U, "pick an ordinal");
    expect(!FindSmokeInputItem(items, "Save@3"), "reject a missing ordinal");
    expect(!FindSmokeInputItem(items, "Hidden"), "ignore invisible items");
}

void testClickRequiresHover()
{
    const std::vector items{ item(7, "Go", { 10, 10 }, { 30, 20 }) };
    auto              commands = ParseSmokeInput("click \"Go\"\n");
    SmokeInputRunner  runner(std::move(*commands));
    const auto        move = runner.Step(items, 0);
    expect(move && std::holds_alternative<SmokeInputMove>(*move), "click first moves the pointer");
    if (move && std::holds_alternative<SmokeInputMove>(*move))
    {
        const auto point = std::get<SmokeInputMove>(*move).logical;
        expect(point.x == 20.0F && point.y == 15.0F, "click targets the item center");
    }
    std::expected<SmokeInputAction, std::string> step;
    for (int frame = 0; frame < 100 && (step = runner.Step(items, 99)) && std::holds_alternative<std::monostate>(*step); ++frame)
    {
    }
    expect(!step && step.error().starts_with("line 1: 'Go' is not hovered"), "fail a click whose target is not hovered");

    SmokeInputRunner hit(std::move(*ParseSmokeInput("click \"Go\"\n")));
    (void)hit.Step(items, 0);
    const auto down = hit.Step(items, 7);
    const auto up   = hit.Step(items, 7);
    expect(down && std::holds_alternative<SmokeInputButton>(*down) && std::get<SmokeInputButton>(*down).down, "press over the hovered item");
    expect(up && std::holds_alternative<SmokeInputButton>(*up) && !std::get<SmokeInputButton>(*up).down, "release after pressing");
    expect(hit.Finished(), "finish after the release");
}

void testExpectAndExit()
{
    const std::vector items{ item(1, "Ready", { 0, 0 }, { 1, 1 }) };
    SmokeInputRunner  runner(std::move(*ParseSmokeInput("expect \"Ready\"\nexpect-missing \"Busy\"\nexpect-exit\n")));
    const auto        waiting = runner.Step(items, 0);
    expect(waiting && std::holds_alternative<std::monostate>(*waiting) && !runner.Finished(), "expectations pass in one frame, then expect-exit waits");
    expect(runner.Exited().has_value() && runner.Finished(), "exit satisfies expect-exit");

    SmokeInputRunner early(std::move(*ParseSmokeInput("wait 2\n")));
    expect(!early.Exited(), "an unexpected exit fails");

    SmokeInputRunner                             missing(std::move(*ParseSmokeInput("expect \"Absent\"\n")));
    std::expected<SmokeInputAction, std::string> step;
    for (int frame = 0; frame < 100 && (step = missing.Step(items, 0)); ++frame)
    {
    }
    expect(!step && step.error().starts_with("line 1: 'Absent' matched 0"), "a missing item fails after the retry budget");
}

} // namespace

int main()
{
    testParse();
    testFind();
    testClickRequiresHover();
    testExpectAndExit();
    if (failures != 0)
    {
        std::cerr << failures << " smoke input test(s) failed\n";
        return 1;
    }
    std::cout << "Smoke input tests passed\n";
    return 0;
}
