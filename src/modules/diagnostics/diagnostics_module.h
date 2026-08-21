// Built-in diagnostics module (P3-04). Always compiled in; the gate's
// reject list is its input, Log/ReportStatus its output until the rendered
// list lands with viewState bindings.
#pragma once

#include <memory>
#include <string>

#include "modules/contract/module_contract.h"

namespace modules {

// Summary text produced by the last diagnostics:refresh dispatch.
const std::wstring& DiagnosticsLastSummary();
// Test hook: a fresh diagnostics descriptor without relying on statics.
std::unique_ptr<ModuleDescriptor> MakeDiagnosticsForTests();

}  // namespace modules
