#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace rerevved::tools
{

struct FileDialogFilter
{
    const wchar_t* name;
    const wchar_t* pattern;
};

enum class NativeDialogError
{
    Unsupported,
    Failed,
};

// A value of std::nullopt means the user cancelled.
using NativeDialogResult = std::expected<std::optional<std::filesystem::path>, NativeDialogError>;

[[nodiscard]] NativeDialogResult OpenFileDialog(const wchar_t* title, std::span<const FileDialogFilter> filters);
[[nodiscard]] NativeDialogResult PickFolderDialog(const wchar_t* title);
// suggested names the file and the folder to start in, when it has one.
[[nodiscard]] NativeDialogResult SaveFileDialog(const wchar_t* title, std::span<const FileDialogFilter> filters, const std::filesystem::path& suggested);

// ImGui text is UTF-8, while std::filesystem::path(const char*) uses the ANSI
// code page on Windows. These keep names outside ANSI intact.
[[nodiscard]] std::filesystem::path PathFromUtf8(std::string_view text);
[[nodiscard]] std::string           PathToUtf8(const std::filesystem::path& path);

bool RevealInFileManager(const std::filesystem::path& path);

} // namespace rerevved::tools
