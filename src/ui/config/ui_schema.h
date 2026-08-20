// The data-driven UI vocabulary (#52): validates assets/ui/core.json itself
// and screen documents against the catalog it carries. Adding a component
// type is a JSON change; nothing here names a component.
//
// Diagnostics carry the source line/column of the offending token, so a bad
// screen file can be reported the way the plan demands: file and line.
//
// This header stays free of windows.h.
#pragma once

#include <string>
#include <vector>

#include "core/json.h"
#include "core/status.h"

namespace ui::config {

struct Diagnostic {
  std::wstring message;
  int line = 0;
  int column = 0;
};

// Validates a core.json document: header fields, token maps, and the shape
// of every catalog entry (kinds, enum values, required flags). Returns Ok
// with empty diagnostics when the document is a valid catalog; otherwise
// fills diagnostics and returns InvalidArgument.
core::Status ValidateCore(const json::Value& core, std::vector<Diagnostic>* diagnostics);

// Validates a screen document against a core catalog that already passed
// ValidateCore. Unknown component types, unknown properties, wrong kinds and
// missing required properties each produce a diagnostic at the offending
// key's line. Children recurse; only types listed in "allows_children" may
// carry them.
core::Status ValidateScreen(const json::Value& core, const json::Value& screen,
                            std::vector<Diagnostic>* diagnostics);

}  // namespace ui::config
