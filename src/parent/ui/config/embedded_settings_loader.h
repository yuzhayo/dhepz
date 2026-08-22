#pragma once

#include <memory>
#include <vector>

#include "core/status.h"
#include "parent/ui/config/resolved_ui_document.h"

namespace ui::config {

core::Status LoadEmbeddedSettingsDocument(
    void* module, std::vector<Diagnostic>* diagnostics,
    std::unique_ptr<ResolvedUiDocument>* document);

}  // namespace ui::config
