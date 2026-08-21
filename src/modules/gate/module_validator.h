// Symmetric module contract validator. It compares all three module halves
// (descriptor, manifest, screens) and returns only accepted routes/modules.
#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/json.h"
#include "core/status.h"
#include "modules/contract/module_contract.h"
#include "modules/contract/module_manifest.h"
#include "modules/registry/module_registry.h"
#include "ui/config/resolved_ui_document.h"

namespace modules {

using RejectEntry = ModuleDiagnosticEntry;
using CapabilityGrant = CapabilityGrantInfo;

struct ValidatedModule {
  ModuleManifest manifest;
  std::unique_ptr<ModuleDescriptor> descriptor;
  bool settings_all_granted = false;
  bool config_write_granted = false;
};

struct ModuleValidationResult {
  json::Value core_catalog;
  ui::config::ScreenSource accepted_base;
  std::unique_ptr<ui::config::ResolvedUiDocument> document;
  std::vector<ValidatedModule> modules;
  std::vector<RejectEntry> rejects;
  std::vector<CapabilityGrant> grants;
  std::vector<std::pair<std::wstring, std::wstring>> action_map;
  std::vector<ui::config::ScreenSource> accepted_sources;
};

class ModuleValidator final {
 public:
  core::Status Validate(std::wstring_view embedded_text,
                        std::wstring_view override_text,
                        const std::vector<RegisteredModule>& registered,
                        ModuleValidationResult* out) const;
  core::Status WithdrawModule(
      const json::Value& core_catalog,
      const std::vector<ui::config::ScreenSource>& current_sources,
      std::wstring_view module_id,
      std::vector<ui::config::ScreenSource>* filtered_sources,
      std::unique_ptr<ui::config::ResolvedUiDocument>* document) const;
};

}  // namespace modules
