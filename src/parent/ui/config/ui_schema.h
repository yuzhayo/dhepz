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

core::Status ValidateCore(const json::Value& core, std::vector<Diagnostic>* diagnostics);
core::Status ValidateScreen(const json::Value& core, const json::Value& screen,
                            std::vector<Diagnostic>* diagnostics);

}  // namespace ui::config
