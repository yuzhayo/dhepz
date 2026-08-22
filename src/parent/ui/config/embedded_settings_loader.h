#pragma once

#include <memory>
#include <vector>

#include "core/status.h"
#include "parent/ui/config/resolved_ui_document.h"

namespace ui::config {

struct EmbeddedScreenResource {
  std::wstring name;
  int resource_id = 0;
};

core::Status LoadEmbeddedSettingsDocument(
    void* module, std::vector<Diagnostic>* diagnostics,
    std::unique_ptr<ResolvedUiDocument>* document);

core::Status LoadEmbeddedFeatureDocument(
    void* module, const std::vector<EmbeddedScreenResource>& resources,
    std::vector<Diagnostic>* diagnostics,
    std::unique_ptr<ResolvedUiDocument>* document);

}  // namespace ui::config
