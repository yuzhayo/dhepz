#pragma once

#include <string>
#include <variant>
#include <vector>

namespace ui::application {

using UiValue =
    std::variant<std::monostate, bool, long long, std::wstring, std::vector<std::wstring>>;

struct UiEvent {
  std::wstring action;
  std::wstring source_id;
  UiValue payload;
};

struct UiChange {
  std::wstring path;
  UiValue value;
};

struct UiPatch {
  std::vector<UiChange> changes;
  std::wstring route;

  bool empty() const { return changes.empty() && route.empty(); }
};

}  // namespace ui::application
