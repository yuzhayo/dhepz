#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace folder_picker {

std::optional<std::wstring> Pick(void* owner_window, std::wstring_view initial_path);

}  // namespace folder_picker
