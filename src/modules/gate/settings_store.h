// Parent-owned, demand-loaded settings persistence.
//
// The object itself is inert: it creates neither a directory nor a worker
// until StartLoad or a write is requested. All file work runs on run-once
// worker threads and every completion settles on the configured UI thread.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/status.h"
#include "modules/contract/module_contract.h"

namespace modules {

enum class SettingsStoreOperation { Load, Save };

struct SettingsStoreCompletion {
  AsyncRequestToken token;
  SettingsStoreOperation operation = SettingsStoreOperation::Load;
  std::uint64_t revision = 0;
  bool used_defaults = false;
  core::Status status;
};

using SettingsStoreCallback =
    std::function<void(const SettingsStoreCompletion& completion)>;

struct SettingsStoreDiagnostic {
  SettingsStoreOperation operation = SettingsStoreOperation::Load;
  std::uint64_t revision = 0;
  core::Status status;
};

class SettingsStore final {
 public:
  // Empty path selects paths::StateDir()/settings.json. A non-empty path is
  // a test seam and is never derived or changed by a module.
  SettingsStore(void* ui_window, unsigned int completion_message,
                std::wstring path = {});
  ~SettingsStore();
  SettingsStore(const SettingsStore&) = delete;
  SettingsStore& operator=(const SettingsStore&) = delete;

  core::Status StartLoad(SettingsStoreCallback callback, AsyncRequestToken* token);

  // Before load completion these return NotFound and clear the output, which
  // is the default snapshot. StartLoad completion is the explicit readiness
  // boundary; callers never infer readiness from a missing key.
  core::Status ReadGlobal(std::wstring_view key, std::wstring* out) const;
  core::Status ReadModule(std::wstring_view module_id, std::wstring_view key,
                          std::wstring* out) const;

  core::Status StartWriteGlobal(std::wstring_view key, std::wstring_view value,
                                SettingsStoreCallback callback,
                                AsyncRequestToken* token);
  core::Status StartWriteModule(std::wstring_view module_id,
                                std::wstring_view key, std::wstring_view value,
                                SettingsStoreCallback callback,
                                AsyncRequestToken* token);
  void CancelRequest(AsyncRequestToken token);

  bool ready() const;
  const std::vector<SettingsStoreDiagnostic>& diagnostics() const;
  void ReapFinished();
  std::size_t ThreadCount() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace modules
