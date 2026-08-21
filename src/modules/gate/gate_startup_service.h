// Parent startup adapter: reads embedded RCDATA in memory and optional user
// override text on a run-once worker. AppGate only orchestrates the result.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "core/status.h"

namespace modules {

using StartupReadCallback =
    std::function<void(core::Status status, std::wstring text)>;

class GateStartupService final {
 public:
  GateStartupService(void* ui_window, unsigned int completion_message);
  ~GateStartupService();
  GateStartupService(const GateStartupService&) = delete;
  GateStartupService& operator=(const GateStartupService&) = delete;

  static core::Status ReadEmbeddedResource(std::wstring_view resource_name,
                                           std::wstring* out);
  core::Status ReadOverride(std::wstring path, StartupReadCallback callback);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace modules
