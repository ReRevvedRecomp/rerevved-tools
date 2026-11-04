#include "inspect/sources_pane.h"
#include "ui/native_dialog.h"
#include "ui/ui_theme.h"
#include "ui/ui_widgets.h"

#include <imgui.h>

#include <algorithm>
#include <string_view>
#include <utility>
#include <vector>

namespace rerevved::tools
{

namespace
{

struct SourceRow
{
    std::uint64_t id;
    unsigned      depth;
};

bool pending(DirectoryState state)
{
    return state == DirectoryState::Queued || state == DirectoryState::Loading;
}

std::string_view directoryStateName(DirectoryState state)
{
    switch (state)
    {
        case DirectoryState::Unloaded:
            return "not listed";
        case DirectoryState::Queued:
            return "queued";
        case DirectoryState::Loading:
            return "listing";
        case DirectoryState::Ready:
            return "listed";
        case DirectoryState::Failed:
            return "failed";
        case DirectoryState::Cancelled:
            return "cancelled";
    }
    return "unknown";
}

std::string pathText(const std::filesystem::path& path)
{
    const auto text = path.u8string();
    return { text.begin(), text.end() };
}

} // namespace

void SourcesPane::RequestBrowse()
{
    const auto chosen = PickFolderDialog(L"Choose a folder of game files");
    if (!chosen)
    {
        requestBrowse = true;
    }
    else if (*chosen)
    {
        setRoot(**chosen);
    }
}

void SourcesPane::setRoot(const std::filesystem::path& path)
{
    browser.SetRoot(path);
    selected = browser.RootId();
    expanded.clear();
    if (selected)
    {
        expanded.insert(*selected);
    }
    openError.clear();
}

void SourcesPane::SetOpenError(std::string error)
{
    openError = error.empty() ? std::string{} : "Open failed: " + error;
}

void SourcesPane::Shutdown()
{
    browser.Shutdown();
}

std::optional<std::filesystem::path> SourcesPane::Root() const
{
    const auto* root = browser.Find(browser.RootId());
    return root ? std::optional(root->path) : std::nullopt;
}

void SourcesPane::drawBrowsePopup()
{
    const bool focus = requestBrowse;
    if (requestBrowse)
    {
        ImGui::OpenPopup("Browse folder");
        requestBrowse = false;
    }
    if (!ImGui::BeginPopupModal("Browse folder", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        return;
    }
    const auto& style = ImGui::GetStyle();
    const auto  width = std::max(1.0F, std::min(600.0F, ImGui::GetMainViewport()->WorkSize.x - style.WindowPadding.x * 2.0F));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
    ImGui::TextUnformatted("Enter a folder path. Only immediate children are listed. "
                           "Assets stay unopened.");
    ImGui::PopTextWrapPos();
    ImGui::SetNextItemWidth(width);
    if (focus)
    {
        ImGui::SetKeyboardFocusHere();
    }
    const bool submitted   = ImGui::InputText("##folder", rootBuffer.data(), rootBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);
    const auto buttonWidth = std::min(120.0F, std::max(1.0F, (width - style.ItemSpacing.x) / 2.0F));
    if (submitted || ImGui::Button("Browse", ImVec2(buttonWidth, 0.0F)))
    {
        setRoot(std::filesystem::path(rootBuffer.data()));
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0.0F)))
    {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

std::optional<std::filesystem::path> SourcesPane::Draw(const std::function<void()>& header)
{
    (void)browser.Poll();
    std::erase_if(expanded, [&](auto id)
                  {
                      return !browser.Find(id);
                  });
    if (selected && !browser.Find(*selected))
    {
        selected.reset();
        openError.clear();
    }
    std::optional<std::filesystem::path> opened;
    const auto                           openSource = [&](const std::filesystem::path& path)
    {
        const auto source = browser.InspectForOpen(path);
        if (source.kind == SourceNodeKind::File && source.error.empty())
        {
            opened = path;
        }
        else
        {
            openError = "Open blocked: " + (source.error.empty() ? std::string("The source is no longer a regular file.") : source.error);
        }
    };
    ImGui::Begin("Sources");
    header();
    ui::SectionHeading("Folder");
    const auto rootId = browser.RootId();
    if (!rootId)
    {
        ui::Muted("No folder chosen.");
        ui::Caption("Browse a folder to list its files. Double click a file to open it.");
    }
    else
    {
        const auto* root = browser.Find(rootId);
        ui::Caption(pathText(root->path));
        if (browser.BusyCount())
        {
            ui::Caption("Listing " + std::to_string(browser.BusyCount()) + " folder(s)...");
        }
        std::vector<SourceRow> rows;
        std::vector<SourceRow> stack{ { rootId, 0 } };
        while (!stack.empty())
        {
            const auto row = stack.back();
            stack.pop_back();
            const auto* node = browser.Find(row.id);
            if (!node)
            {
                continue;
            }
            rows.push_back(row);
            if (expanded.contains(row.id))
            {
                for (auto child = node->children.rbegin(); child != node->children.rend(); ++child)
                {
                    stack.push_back({ *child, row.depth + 1 });
                }
            }
        }
        const auto listHeight = std::max(100.0F, ImGui::GetContentRegionAvail().y * 0.5F);
        ImGui::BeginChild("Source rows", ImVec2(0.0F, listHeight), ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened);
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rows.size()), ImGui::GetFrameHeightWithSpacing());
        while (clipper.Step())
        {
            for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
            {
                const auto  row  = rows[static_cast<std::size_t>(index)];
                const auto* node = browser.Find(row.id);
                ImGui::PushID(static_cast<int>(row.id));
                const auto indent = std::min(row.depth, 8U) * ImGui::GetStyle().IndentSpacing;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
                const bool directory  = node->kind == SourceNodeKind::Directory;
                const bool isExpanded = expanded.contains(row.id);
                bool       toggle     = false;
                if (directory)
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_Text, ui::color::Muted);
                    toggle = ImGui::Button(isExpanded ? ui::icon::ChevronDown : ui::icon::ChevronRight, ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()));
                    ImGui::PopStyleColor(2);
                }
                else
                {
                    ImGui::Dummy(ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()));
                }
                ImGui::SameLine();
                auto name = pathText(node->path.filename());
                if (name.empty())
                {
                    name = pathText(node->path);
                }
                if (node->kind == SourceNodeKind::ReparsePoint)
                {
                    name += " [link, blocked]";
                }
                else if (!node->error.empty() && !directory)
                {
                    name += " [metadata error]";
                }
                else if (directory)
                {
                    if (node->state != DirectoryState::Ready && node->state != DirectoryState::Unloaded)
                    {
                        name += " [" + std::string(directoryStateName(node->state)) + "]";
                    }
                    if (node->isPartial)
                    {
                        name += " [partial]";
                    }
                }
                const bool activated = ImGui::Selectable("##source", selected == row.id, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0.0F, ImGui::GetFrameHeight()));
                const auto rowMin    = ImGui::GetItemRectMin();
                const auto rowMax    = ImGui::GetItemRectMax();
                ImGui::GetWindowDrawList()->PushClipRect(rowMin, rowMax, true);
                const auto textY = rowMin.y + ImGui::GetStyle().FramePadding.y;
                ImGui::GetWindowDrawList()->AddText(ImVec2(rowMin.x, textY), directory ? ui::color::Accent : ui::color::Muted, directory ? ui::icon::Folder : ui::icon::File);
                ImGui::GetWindowDrawList()->AddText(ImVec2(rowMin.x + 24.0F, textY), ImGui::GetColorU32(ImGuiCol_Text), name.c_str());
                ImGui::GetWindowDrawList()->PopClipRect();
                if (activated)
                {
                    selected = row.id;
                    openError.clear();
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    {
                        if (directory)
                        {
                            toggle = true;
                        }
                        else if (node->kind == SourceNodeKind::File)
                        {
                            openSource(node->path);
                        }
                    }
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("%s", pathText(node->path).c_str());
                }
                if (toggle)
                {
                    selected = row.id;
                    openError.clear();
                    if (isExpanded)
                    {
                        expanded.erase(row.id);
                    }
                    else
                    {
                        expanded.insert(row.id);
                        if (node->state == DirectoryState::Unloaded)
                        {
                            if (!browser.Request(row.id))
                            {
                                openError = "Directory request unavailable. Retry when pending listings finish.";
                            }
                        }
                    }
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
        if (selected)
        {
            const auto* node = browser.Find(*selected);
            ImGui::TextWrapped("Selected: %s", pathText(node->path).c_str());
            if (node->kind == SourceNodeKind::Directory)
            {
                ImGui::TextWrapped("Directory: %s", directoryStateName(node->state).data());
                if (pending(node->state))
                {
                    if (ImGui::Button("Cancel Listing"))
                    {
                        (void)browser.Cancel(*node->request);
                    }
                }
                else if (ImGui::Button("Retry / refresh"))
                {
                    expanded.insert(node->id);
                    if (!browser.Request(node->id))
                    {
                        openError = "Directory request unavailable. Retry when pending listings finish.";
                    }
                    else
                    {
                        openError.clear();
                    }
                }
                if (node->isPartial)
                {
                    ImGui::TextWrapped("Partial listing: %s", node->partialLimitReason.c_str());
                }
            }
            else if (node->kind == SourceNodeKind::ReparsePoint)
            {
                ImGui::TextWrapped("Reparse point: traversal and Sources Open are blocked.");
            }
            else if (node->kind == SourceNodeKind::File)
            {
                if (ImGui::Button("Open"))
                {
                    openSource(node->path);
                }
            }
            else
            {
                ImGui::TextWrapped("This source is not a regular file or directory.");
            }
            if (!node->error.empty())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0F, 0.75F, 0.25F, 1.0F));
                ImGui::TextWrapped("%s", node->error.c_str());
                ImGui::PopStyleColor();
            }
        }
        if (!openError.empty())
        {
            ImGui::TextWrapped("%s", openError.c_str());
        }
    }
    drawBrowsePopup();
    ImGui::End();
    return opened;
}

} // namespace rerevved::tools
