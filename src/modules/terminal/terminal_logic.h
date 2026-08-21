// Pure terminal launch modelling. It parses screen payload data into a typed
// spec and emits the parent contract's executable + argv request. Quoting and
// process creation remain parent-owned.
#pragma once

#include <string>

#include "core/json.h"
#include "core/status.h"
#include "modules/contract/module_contract.h"

namespace terminal {

enum class Shell { PowerShell, Cmd, Wsl };
enum class PathKind { None, Windows, Linux };

struct VenvSelection {
  PathKind kind = PathKind::None;
  std::wstring activate_path;
};

struct LaunchSpec {
  Shell shell = Shell::PowerShell;
  std::wstring working_dir;
  std::wstring wsl_distro;
  bool admin = false;
  VenvSelection venv;
};

core::Status ParseLaunchPayload(const json::Value& payload, LaunchSpec* out);
core::Status BuildProcessRequest(const LaunchSpec& spec,
                                 modules::ProcessRequest* out);

}  // namespace terminal
