#include "platform/jump_list.h"

#include <windows.h>
#include <shobjidl.h>
#include <propsys.h>
#include <propkey.h>
#include <propvarutil.h>
#include <wrl/client.h>

#include <algorithm>

#include "platform/paths.h"
#include "platform/strings.h"

namespace jump_list {
namespace {

using Microsoft::WRL::ComPtr;

core::Status Failed(std::wstring_view operation) {
  return DHEPZ_ERR(core::ErrorCode::Internal,
                   std::wstring(operation) + L" failed while updating the taskbar Jump List");
}

core::Status AddTask(IObjectCollection* tasks, std::wstring_view executable,
                     std::wstring_view route, std::wstring_view label) {
  ComPtr<IShellLinkW> link;
  if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&link)))) {
    return Failed(L"Creating a Jump List task");
  }
  if (FAILED(link->SetPath(std::wstring(executable).c_str()))) {
    return Failed(L"Setting the Jump List executable");
  }
  if (!route.empty()) {
    const std::wstring arguments = L"--route " + str::QuoteArg(route);
    if (FAILED(link->SetArguments(arguments.c_str()))) {
      return Failed(L"Setting Jump List route arguments");
    }
  }
  (void)link->SetIconLocation(std::wstring(executable).c_str(), 0);

  ComPtr<IPropertyStore> properties;
  if (FAILED(link.As(&properties))) return Failed(L"Opening Jump List task properties");
  PROPVARIANT title{};
  if (FAILED(InitPropVariantFromString(std::wstring(label).c_str(), &title))) {
    return Failed(L"Creating the Jump List task title");
  }
  const HRESULT titled = properties->SetValue(PKEY_Title, title);
  PropVariantClear(&title);
  if (FAILED(titled) || FAILED(properties->Commit())) {
    return Failed(L"Saving the Jump List task title");
  }
  return SUCCEEDED(tasks->AddObject(link.Get())) ? core::Ok()
                                                 : Failed(L"Adding the Jump List task");
}

}  // namespace

core::Status Update(const std::vector<std::wstring>& routes,
                    const std::vector<std::wstring>& labels) {
  const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const bool uninitialize = SUCCEEDED(initialized);
  if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) {
    return Failed(L"Initializing COM");
  }

  core::Status status = core::Ok();
  ComPtr<ICustomDestinationList> destination;
  if (FAILED(CoCreateInstance(CLSID_DestinationList, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&destination)))) {
    status = Failed(L"Creating the destination list");
  }

  UINT slots = 0;
  ComPtr<IObjectArray> removed;
  if (status.ok() && FAILED(destination->BeginList(&slots, IID_PPV_ARGS(&removed)))) {
    status = Failed(L"Beginning the destination list");
  }

  ComPtr<IObjectCollection> tasks;
  if (status.ok() &&
      FAILED(CoCreateInstance(CLSID_EnumerableObjectCollection, nullptr,
                              CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&tasks)))) {
    status = Failed(L"Creating the Jump List task collection");
  }

  const std::wstring executable = paths::ExecutablePath();
  if (status.ok() && executable.empty()) status = Failed(L"Resolving the executable path");
  if (status.ok() && routes.empty()) {
    status = AddTask(tasks.Get(), executable, {}, L"dhepz");
  }
  const std::size_t count = std::min(routes.size(), labels.size());
  for (std::size_t index = 0; status.ok() && index < count; ++index) {
    status = AddTask(tasks.Get(), executable, routes[index], labels[index]);
  }
  if (status.ok() && FAILED(destination->AddUserTasks(tasks.Get()))) {
    status = Failed(L"Publishing Jump List tasks");
  }
  if (status.ok() && FAILED(destination->CommitList())) {
    status = Failed(L"Committing the destination list");
  } else if (!status.ok() && destination != nullptr) {
    destination->AbortList();
  }
  if (uninitialize) CoUninitialize();
  return status;
}

}  // namespace jump_list
