#include "parent/ui/contracts/ui_action_registry.h"

#include <algorithm>

namespace ui::application {

bool UiActionRegistry::Register(std::wstring action, Handler handler) {
  if (action.empty() || !handler || Has(action)) return false;
  handlers_.emplace_back(std::move(action), std::move(handler));
  return true;
}

bool UiActionRegistry::Replace(std::wstring_view action, Handler handler) {
  if (!handler) return false;
  const auto found = std::find_if(handlers_.begin(), handlers_.end(), [action](const auto& item) {
    return item.first == action;
  });
  if (found == handlers_.end()) return false;
  found->second = std::move(handler);
  return true;
}

UiPatch UiActionRegistry::Dispatch(const UiEvent& event, const UiState& state) const {
  const auto found = std::find_if(handlers_.begin(), handlers_.end(), [&event](const auto& item) {
    return item.first == event.action;
  });
  return found == handlers_.end() ? UiPatch{} : found->second(event, state);
}

bool UiActionRegistry::Has(std::wstring_view action) const {
  return std::any_of(handlers_.begin(), handlers_.end(), [action](const auto& item) {
    return item.first == action;
  });
}

}  // namespace ui::application
