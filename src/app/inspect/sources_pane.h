#pragma once

#include "inspect/source_browser.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_set>

namespace rerevved::tools
{

class SourcesPane
{
public:
    [[nodiscard]] std::optional<std::filesystem::path> Draw(const std::function<void()>& header);
    void                                               RequestBrowse();
    void                                               SetOpenError(std::string error);
    void                                               Shutdown();
    [[nodiscard]] std::optional<std::filesystem::path> Root() const;

private:
    void drawBrowsePopup();
    void setRoot(const std::filesystem::path& path);

    SourceBrowser                     browser;
    std::array<char, 2048>            rootBuffer{};
    std::optional<std::uint64_t>      selected;
    std::unordered_set<std::uint64_t> expanded;
    std::string                       openError;
    bool                              requestBrowse = false;
};

} // namespace rerevved::tools
