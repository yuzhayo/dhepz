#include "platform/folder_picker.h"

#include <windows.h>
#include <shobjidl.h>

namespace folder_picker {

std::optional<std::wstring> Pick(void* owner_window, std::wstring_view initial_path) {
  const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const bool uninitialize = initialized == S_OK || initialized == S_FALSE;
  IFileOpenDialog* dialog = nullptr;
  HRESULT result = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&dialog));
  if (FAILED(result) || dialog == nullptr) {
    if (uninitialize) CoUninitialize();
    return std::nullopt;
  }
  DWORD options = 0;
  if (SUCCEEDED(dialog->GetOptions(&options))) {
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
  }
  dialog->SetTitle(L"Pilih folder untuk terminal");
  if (!initial_path.empty()) {
    IShellItem* initial = nullptr;
    const std::wstring path(initial_path);
    if (SUCCEEDED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&initial)))) {
      dialog->SetFolder(initial);
      initial->Release();
    }
  }
  std::optional<std::wstring> selected;
  if (SUCCEEDED(dialog->Show(static_cast<HWND>(owner_window)))) {
    IShellItem* item = nullptr;
    if (SUCCEEDED(dialog->GetResult(&item)) && item != nullptr) {
      PWSTR raw = nullptr;
      if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw)) && raw != nullptr) {
        selected = std::wstring(raw);
        CoTaskMemFree(raw);
      }
      item->Release();
    }
  }
  dialog->Release();
  if (uninitialize) CoUninitialize();
  return selected;
}

}  // namespace folder_picker
