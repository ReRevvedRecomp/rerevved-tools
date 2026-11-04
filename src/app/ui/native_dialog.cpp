#include "ui/native_dialog.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#endif

#include <string_view>
#include <vector>

namespace rerevved::tools
{

std::filesystem::path PathFromUtf8(std::string_view text)
{
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

std::string PathToUtf8(const std::filesystem::path& path)
{
    const auto text = path.u8string();
    return { text.begin(), text.end() };
}

#ifdef _WIN32
namespace
{

template <typename T>
struct ComPointer
{
    T* value = nullptr;

    ~ComPointer()
    {
        if (value != nullptr)
        {
            value->Release();
        }
    }
};

struct ComScope
{
    HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    ~ComScope()
    {
        // RPC_E_CHANGED_MODE means COM uses another mode and is not ours to release.
        if (SUCCEEDED(result))
        {
            CoUninitialize();
        }
    }
};

NativeDialogResult present(IFileDialog* dialog, const wchar_t* title, std::span<const FileDialogFilter> filters, FILEOPENDIALOGOPTIONS extra)
{
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR | extra);
    dialog->SetTitle(title);
    if (!filters.empty())
    {
        std::vector<COMDLG_FILTERSPEC> specs;
        for (const auto& filter : filters)
        {
            specs.push_back(COMDLG_FILTERSPEC{ filter.name, filter.pattern });
        }
        dialog->SetFileTypes(static_cast<UINT>(specs.size()), specs.data());
    }
    const HRESULT shown = dialog->Show(GetActiveWindow());
    if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED))
    {
        return std::optional<std::filesystem::path>{};
    }
    if (FAILED(shown))
    {
        return std::unexpected(NativeDialogError::Failed);
    }
    ComPointer<IShellItem> item;
    if (FAILED(dialog->GetResult(&item.value)))
    {
        return std::unexpected(NativeDialogError::Failed);
    }
    PWSTR path = nullptr;
    if (FAILED(item.value->GetDisplayName(SIGDN_FILESYSPATH, &path)))
    {
        return std::unexpected(NativeDialogError::Failed);
    }
    std::filesystem::path result(path);
    CoTaskMemFree(path);
    return std::optional(std::move(result));
}

NativeDialogResult show(const wchar_t* title, std::span<const FileDialogFilter> filters, bool folders)
{
    ComScope com;
    if (FAILED(com.result) && com.result != RPC_E_CHANGED_MODE)
    {
        return std::unexpected(NativeDialogError::Failed);
    }
    ComPointer<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileOpenDialog, reinterpret_cast<void**>(&dialog.value))))
    {
        return std::unexpected(NativeDialogError::Failed);
    }
    return present(dialog.value, title, filters, folders ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST);
}

} // namespace

NativeDialogResult OpenFileDialog(const wchar_t* title, std::span<const FileDialogFilter> filters)
{
    return show(title, filters, false);
}

NativeDialogResult PickFolderDialog(const wchar_t* title)
{
    return show(title, {}, true);
}

NativeDialogResult SaveFileDialog(const wchar_t* title, std::span<const FileDialogFilter> filters, const std::filesystem::path& suggested)
{
    ComScope com;
    if (FAILED(com.result) && com.result != RPC_E_CHANGED_MODE)
    {
        return std::unexpected(NativeDialogError::Failed);
    }
    ComPointer<IFileSaveDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileSaveDialog, reinterpret_cast<void**>(&dialog.value))))
    {
        return std::unexpected(NativeDialogError::Failed);
    }
    // "*.rrmap" gives the extension added to a name typed without one.
    if (!filters.empty() && std::wstring_view(filters.front().pattern).starts_with(L"*."))
    {
        dialog.value->SetDefaultExtension(filters.front().pattern + 2);
    }
    dialog.value->SetFileName(suggested.filename().c_str());
    ComPointer<IShellItem> folder;
    if (suggested.has_parent_path() && SUCCEEDED(SHCreateItemFromParsingName(suggested.parent_path().c_str(), nullptr, IID_IShellItem, reinterpret_cast<void**>(&folder.value))))
    {
        dialog.value->SetFolder(folder.value);
    }
    return present(dialog.value, title, filters, FOS_OVERWRITEPROMPT);
}

bool RevealInFileManager(const std::filesystem::path& path)
{
    const auto result = ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
}
#else
NativeDialogResult OpenFileDialog(const wchar_t*, std::span<const FileDialogFilter>)
{
    return std::unexpected(NativeDialogError::Unsupported);
}

NativeDialogResult PickFolderDialog(const wchar_t*)
{
    return std::unexpected(NativeDialogError::Unsupported);
}

NativeDialogResult SaveFileDialog(const wchar_t*, std::span<const FileDialogFilter>, const std::filesystem::path&)
{
    return std::unexpected(NativeDialogError::Unsupported);
}

bool RevealInFileManager(const std::filesystem::path&)
{
    return false;
}
#endif

} // namespace rerevved::tools
