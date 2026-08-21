// Parent-owned implementation of ModuleHost's asynchronous process and
// folder-probe services. Work runs on run-once worker threads; every result is
// posted back to the configured UI window and guarded by one host generation.
#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>

#include "modules/contract/module_contract.h"

namespace modules {

using StatePatchSink = std::function<core::Status(const json::Value& patch)>;

// Builds argv[0] plus the structured arguments with the one parent-owned
// quoting path used by normal launch and capture.
core::Status BuildProcessCommandLine(const ProcessRequest& request, std::wstring* out);

class HostOperationDispatcher final {
 public:
  HostOperationDispatcher(void* ui_window, unsigned int completion_message,
                          StatePatchSink state_patch_sink);
  ~HostOperationDispatcher();

  HostOperationDispatcher(const HostOperationDispatcher&) = delete;
  HostOperationDispatcher& operator=(const HostOperationDispatcher&) = delete;

  core::Status StartProcess(const ProcessRequest& request, HostOperationCallback callback,
                            AsyncRequestToken* token);
  core::Status StartFolderProbe(const FolderProbeRequest& request,
                                HostOperationCallback callback, AsyncRequestToken* token);
  // Parent-internal service used by config:write. Modules cannot choose the
  // destination; the transaction service supplies its configured path.
  core::Status StartAtomicWrite(std::wstring path, std::wstring text,
                                HostOperationCallback callback,
                                AsyncRequestToken* token);
  void CancelRequest(AsyncRequestToken token);

  // Permanently invalidates this module-host lifetime. Running jobs may finish
  // their OS work, but no queued or future completion can be delivered.
  void InvalidateGeneration();

  // Only succeeds on the UI thread captured at construction.
  core::Status PublishStatePatch(const json::Value& patch);

  void ReapFinished();
  std::size_t ThreadCount() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace modules
