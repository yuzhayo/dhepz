#pragma once

#include <string_view>

#include "core/status.h"

namespace explorer_context_menu {

core::Status Install(std::wstring_view executable);
core::Status Remove();

}  // namespace explorer_context_menu
