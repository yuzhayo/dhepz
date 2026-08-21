// Pure terminal launch modelling. The child chooses terminal policy; the
// parent receives only a structured executable + argv request.
#pragma once

#include <string>

#include "core/json.h"
#include "core/status.h"
#include "modules/contract/module_contract.h"

namespace terminal {

enum class Target { PowerShell, PowerShellAdmin, Wsl };

struct LaunchSpec {
  Target target = Target::PowerShell;
  std::wstring working_folder;
  std::wstring wsl_distro;
  bool venv_enabled = false;
};

core::Status ParseLaunchPayload(const json::Value& payload, LaunchSpec* out);
core::Status BuildProcessRequest(const LaunchSpec& spec,
                                 modules::ProcessRequest* out);

}  // namespace terminal
