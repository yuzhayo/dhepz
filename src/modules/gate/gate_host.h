// Parent-side ModuleHost adapter. Modules only see ModuleHost; this adapter
// translates that contract into the services owned by the application.
#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "modules/contract/module_contract.h"

namespace modules {

class HostOperationDispatcher;
class ConfigTransactionService;
class SettingsAccessService;

using HostStatePatchHandler =
    std::function<core::Status(std::wstring_view module_id,
                               const json::Value& patch)>;
using HostRouteRequestHandler =
    std::function<core::Status(std::wstring_view route_id)>;
using HostPeersHandler = std::function<std::vector<PeerInfo>()>;

class GateHost final : public ModuleHost,
                       public SettingsAllFacet,
                       public ConfigWriteFacet {
 public:
  GateHost(std::wstring module_id,
           HostRouteRequestHandler route_request_handler,
           HostPeersHandler peers_handler,
           SettingsAccessService* settings_service,
           ConfigTransactionService* config_service,
           bool settings_all_granted, bool config_write_granted,
           void* operation_window,
           unsigned int operation_message,
           HostStatePatchHandler state_patch_handler);
  ~GateHost() override;

  ModuleSurface Surface() override;
  core::Status SettingsRead(std::wstring_view key, std::wstring* out) override;
  core::Status SettingsReadGlobal(std::wstring_view key, std::wstring* out) override;
  core::Status SettingsWrite(std::wstring_view key, std::wstring_view value) override;
  core::Status StartSettingsLoad(HostOperationCallback callback,
                                 AsyncRequestToken* token) override;
  core::Status StartProcess(const ProcessRequest& request,
                            HostOperationCallback callback,
                            AsyncRequestToken* token) override;
  core::Status StartFolderProbe(const FolderProbeRequest& request,
                                HostOperationCallback callback,
                                AsyncRequestToken* token) override;
  void CancelRequest(AsyncRequestToken token) override;
  core::Status PublishStatePatch(const json::Value& patch) override;
  core::Status GetSettingsAllFacet(SettingsAllFacet** facet) override;
  core::Status GetConfigWriteFacet(ConfigWriteFacet** facet) override;
  core::Status ReadGlobal(std::wstring_view key, std::wstring* out) override;
  core::Status WriteGlobal(std::wstring_view key,
                           std::wstring_view value) override;
  core::Status ReadModule(std::wstring_view module_id,
                          std::wstring_view key,
                          std::wstring* out) override;
  core::Status WriteModule(std::wstring_view module_id,
                           std::wstring_view key,
                           std::wstring_view value) override;
  core::Status Preview(std::wstring_view candidate,
                       ConfigPreviewResult* result) override;
  core::Status Save(ConfigPreviewToken preview,
                    HostOperationCallback callback,
                    AsyncRequestToken* request) override;
  core::Status Discard(
      ConfigPreviewToken preview,
      std::vector<std::wstring>* affected_routes) override;
  void ReportStatus(const core::Status& status) override;
  void Log(std::wstring_view level, std::wstring_view text) override;
  core::Status RequestRoute(std::wstring_view route_id) override;
  std::vector<PeerInfo> Peers() override;

 private:
  std::wstring module_id_;
  HostRouteRequestHandler route_request_handler_;
  HostPeersHandler peers_handler_;
  SettingsAccessService* settings_service_;
  ConfigTransactionService* config_service_;
  bool settings_all_granted_ = false;
  bool config_write_granted_ = false;
  ModuleSurface surface_;
  std::mutex mutex_;
  std::vector<std::wstring> log_;
  core::Status last_status_;
  std::unique_ptr<HostOperationDispatcher> operations_;
};

}  // namespace modules
