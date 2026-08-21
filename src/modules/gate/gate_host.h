// Parent-side ModuleHost adapter. Modules only see ModuleHost; this adapter
// translates that contract into the services owned by the application.
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "modules/contract/module_contract.h"

namespace modules {

class HostOperationDispatcher;

using HostStatePatchHandler =
    std::function<core::Status(std::wstring_view module_id,
                               const json::Value& patch)>;
using HostRouteRequestHandler =
    std::function<core::Status(std::wstring_view route_id)>;
using HostPeersHandler = std::function<std::vector<PeerInfo>()>;

class GateHost final : public ModuleHost {
 public:
  GateHost(std::wstring module_id,
           HostRouteRequestHandler route_request_handler,
           HostPeersHandler peers_handler, void* operation_window,
           unsigned int operation_message,
           HostStatePatchHandler state_patch_handler);
  ~GateHost() override;

  ModuleSurface Surface() override;
  core::Status SettingsRead(std::wstring_view key, std::wstring* out) override;
  core::Status SettingsReadGlobal(std::wstring_view key, std::wstring* out) override;
  core::Status SettingsWrite(std::wstring_view key, std::wstring_view value) override;
  core::Status StorageWrite(std::wstring_view name, std::wstring_view data) override;
  core::Status StorageRead(std::wstring_view name, std::wstring* out) override;
  core::Status StartProcess(const ProcessRequest& request,
                            HostOperationCallback callback,
                            AsyncRequestToken* token) override;
  core::Status StartFolderProbe(const FolderProbeRequest& request,
                                HostOperationCallback callback,
                                AsyncRequestToken* token) override;
  void CancelRequest(AsyncRequestToken token) override;
  core::Status PublishStatePatch(const json::Value& patch) override;
  void ReportStatus(const core::Status& status) override;
  void Log(std::wstring_view level, std::wstring_view text) override;
  core::Status RequestRoute(std::wstring_view route_id) override;
  std::vector<PeerInfo> Peers() override;

 private:
  std::wstring module_id_;
  HostRouteRequestHandler route_request_handler_;
  HostPeersHandler peers_handler_;
  ModuleSurface surface_;
  std::mutex mutex_;
  std::map<std::wstring, std::map<std::wstring, std::wstring>> settings_;
  std::map<std::wstring, std::wstring> global_;
  std::map<std::wstring, std::wstring> storage_;
  std::vector<std::wstring> log_;
  core::Status last_status_;
  std::unique_ptr<HostOperationDispatcher> operations_;
};

}  // namespace modules
