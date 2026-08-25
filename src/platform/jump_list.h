#pragma once

#include <string>
#include <vector>

#include "core/status.h"

namespace jump_list {

// Replaces the current process Jump List tasks with module routes. When no
// route is visible, one plain "dhepz" task keeps the pinned shortcut useful.
core::Status Update(const std::vector<std::wstring>& routes,
                    const std::vector<std::wstring>& labels);

}  // namespace jump_list
