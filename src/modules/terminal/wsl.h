// Pure parser for decoded `wsl.exe -l -q` output. Process execution and cache
// ownership live in TerminalModule through the parent host contract.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "modules/contract/module_contract.h"

namespace terminal {

std::vector<std::wstring> ParseWslListOutput(std::wstring_view output);

// Session-owned terminal feature state. External work is available only via
// ModuleHost; the helper neither names nor includes a platform service.
class WslSession final {
 public:
  void WriteDefaults(json::Value* patch) const;
  core::Status Bind(modules::ModuleHost& host);
  core::Status Refresh(json::Value* immediate_patch);
  void Release();

 private:
  modules::ModuleHost* host_ = nullptr;
  modules::AsyncRequestToken token_;
  std::vector<std::wstring> distros_;
  std::uint64_t generation_ = 0;
  std::uint64_t refresh_ = 0;
  bool cached_ = false;
};

}  // namespace terminal
