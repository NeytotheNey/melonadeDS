#include "FileDialog.h"

#include <windows.h>
#include <shobjidl.h>
#include <SDL_syswm.h>

#include "Settings.h"

namespace Frontend
{

namespace
{

std::optional<std::string> ShowOpenDialog(SDL_Window* parent, const std::string& startDir, bool folders)
{
    HWND owner = nullptr;
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (parent && SDL_GetWindowWMInfo(parent, &info) && info.subsystem == SDL_SYSWM_WINDOWS)
        owner = info.info.win.window;

    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileOpenDialog, (void**)&dialog)) || !dialog)
        return std::nullopt;

    DWORD options = 0;
    dialog->GetOptions(&options);
    if (folders)
    {
        dialog->SetTitle(L"Choose a folder");
        dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_PICKFOLDERS | FOS_PATHMUSTEXIST);
    }
    else
    {
        const COMDLG_FILTERSPEC filters[] = {
            {L"DS games (*.nds, *.dsi)", L"*.nds;*.dsi"},
            {L"All files", L"*.*"},
        };
        dialog->SetFileTypes(2, filters);
        dialog->SetFileTypeIndex(1);
        dialog->SetTitle(L"Open ROM");
        dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);
    }
    if (!startDir.empty())
    {
        IShellItem* folder = nullptr;
        std::wstring wide = PathFromUtf8(startDir).wstring();
        if (SUCCEEDED(SHCreateItemFromParsingName(wide.c_str(), nullptr, IID_IShellItem, (void**)&folder)) && folder)
        {
            dialog->SetFolder(folder);
            folder->Release();
        }
    }

    std::optional<std::string> result;
    if (SUCCEEDED(dialog->Show(owner)))
    {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)) && item)
        {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path)
            {
                result = PathToUtf8(std::filesystem::path(path));
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
    return result;
}

}

std::optional<std::string> OpenRomDialog(SDL_Window* parent, const std::string& startDir)
{
    return ShowOpenDialog(parent, startDir, false);
}

std::optional<std::string> OpenFolderDialog(SDL_Window* parent, const std::string& startDir)
{
    return ShowOpenDialog(parent, startDir, true);
}

}
