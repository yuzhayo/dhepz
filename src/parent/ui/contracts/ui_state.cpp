#include "parent/ui/contracts/ui_state.h"

#include <algorithm>

namespace ui::application {

const UiValue* UiState::Find(std::wstring_view path) const {
  const auto found = std::find_if(values_.begin(), values_.end(), [path](const auto& item) {
    return item.first == path;
  });
  return found == values_.end() ? nullptr : &found->second;
}

void UiState::Set(std::wstring path, UiValue value) {
  const auto found = std::find_if(values_.begin(), values_.end(), [&path](const auto& item) {
    return item.first == path;
  });
  if (found == values_.end()) {
    values_.emplace_back(std::move(path), std::move(value));
    ++revision_;
  } else {
    if (found->second == value) return;
    found->second = std::move(value);
    ++revision_;
  }
}

bool UiState::Apply(const UiPatch& patch) {
  const std::uint64_t before = revision_;
  for (const UiChange& change : patch.changes) Set(change.path, change.value);
  return revision_ != before || !patch.route.empty();
}

bool UiState::Bool(std::wstring_view path, bool fallback) const {
  const UiValue* value = Find(path);
  const bool* typed = value != nullptr ? std::get_if<bool>(value) : nullptr;
  return typed != nullptr ? *typed : fallback;
}

long long UiState::Integer(std::wstring_view path, long long fallback) const {
  const UiValue* value = Find(path);
  const long long* typed = value != nullptr ? std::get_if<long long>(value) : nullptr;
  return typed != nullptr ? *typed : fallback;
}

std::wstring UiState::Text(std::wstring_view path, std::wstring_view fallback) const {
  const UiValue* value = Find(path);
  const std::wstring* typed = value != nullptr ? std::get_if<std::wstring>(value) : nullptr;
  return typed != nullptr ? *typed : std::wstring(fallback);
}

const std::vector<std::wstring>* UiState::Strings(std::wstring_view path) const {
  const UiValue* value = Find(path);
  return value != nullptr ? std::get_if<std::vector<std::wstring>>(value) : nullptr;
}

}  // namespace ui::application
