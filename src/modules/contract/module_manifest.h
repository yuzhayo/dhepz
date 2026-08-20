// module.json — the contract in writing (P3-01). The gate (P3-03) validates
// the two halves agree; this validator checks the manifest itself, with
// file+line diagnostics from the JSON parser positions.
#pragma once

#include <string>
#include <vector>

#include "core/status.h"

namespace modules {

// The only capabilities that exist (plan Part 3). Anything else is a
// contract violation and the module is rejected.
inline constexpr std::wstring_view kCapabilitySettingsAll = L"settings:all";
inline constexpr std::wstring_view kCapabilityConfigWrite = L"config:write";

struct ModuleManifest {
  std::wstring module_id;
  std::wstring tab_label;
  int order = 100;
  bool show_in_tabs = true;
  std::wstring settings_route;  // empty when absent
  std::vector<std::wstring> actions;
  std::vector<std::wstring> bindings;
  std::vector<std::wstring> capabilities;
};

struct ManifestDiagnostic {
  std::wstring message;
  int line = 0;
  int column = 0;
};

// Parses and validates a module.json document. Ok + filled manifest when
// clean; otherwise diagnostics carry line/column of each offending key.
core::Status ParseManifest(std::wstring_view json_text, ModuleManifest* out,
                           std::vector<ManifestDiagnostic>* diagnostics);

}  // namespace modules
