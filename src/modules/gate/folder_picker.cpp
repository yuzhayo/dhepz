#include "modules/gate/folder_picker.h"

#include <windows.h>
#include <shobjidl.h>

namespace modules {

core::Status ShowNativeFolderPicker(void* owner_window,
                                    const FolderPickerRequest& request,
                                    FolderPickerResult* result) {
  if (result == nullptr) {
    return core::Err(core::ErrorCode::InvalidArgument,
                     L"folder picker result is required");
  }
  result->directory.clear();

  IFileOpenDialog* dialog = nullptr;
  HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
  if (FAILED(hr)) {
    return core::Err(core::ErrorCode::IoError,
                     L"native folder picker could not be created");
  }

  DWORD options = 0;
  hr = dialog->GetOptions(&options);
  if (SUCCEEDED(hr)) {
    hr = dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM |
                            FOS_PATHMUSTEXIST);
  }
  if (SUCCEEDED(hr) && !request.title.empty()) {
    hr = dialog->SetTitle(request.title.c_str());
  }
  if (SUCCEEDED(hr) && !request.initial_directory.empty()) {
    IShellItem* initial = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(request.initial_directory.c_str(),
                                               nullptr,
                                               IID_PPV_ARGS(&initial)))) {
      const HRESULT set_folder = dialog->SetFolder(initial);
      (void)set_folder;
      initial->Release();
    }
  }

  if (SUCCEEDED(hr)) hr = dialog->Show(static_cast<HWND>(owner_window));
  if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
    dialog->Release();
    return core::Err(core::ErrorCode::Cancelled,
                     L"folder selection was cancelled");
  }
  if (FAILED(hr)) {
    dialog->Release();
    return core::Err(core::ErrorCode::IoError,
                     L"native folder picker failed");
  }

  IShellItem* selected = nullptr;
  hr = dialog->GetResult(&selected);
  dialog->Release();
  if (FAILED(hr) || selected == nullptr) {
    return core::Err(core::ErrorCode::IoError,
                     L"selected folder is unavailable");
  }

  PWSTR path = nullptr;
  hr = selected->GetDisplayName(SIGDN_FILESYSPATH, &path);
  selected->Release();
  if (FAILED(hr) || path == nullptr) {
    return core::Err(core::ErrorCode::IoError,
                     L"selected folder has no filesystem path");
  }
  result->directory = path;
  CoTaskMemFree(path);
  return core::Ok();
}

}  // namespace modules
