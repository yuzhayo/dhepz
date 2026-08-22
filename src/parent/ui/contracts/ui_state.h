#pragma once

#include <string_view>
#include <vector>

#include "parent/ui/contracts/ui_contract.h"

namespace ui::application {

class UiState final {
 public:
  const UiValue* Find(std::wstring_view path) const;
  void Set(std::wstring path, UiValue value);
  bool Apply(const UiPatch& patch);

  bool Bool(std::wstring_view path, bool fallback = false) const;
  long long Integer(std::wstring_view path, long long fallback = 0) const;
  std::wstring Text(std::wstring_view path, std::wstring_view fallback = {}) const;
  const std::vector<std::wstring>* Strings(std::wstring_view path) const;

 private:
  std::vector<std::pair<std::wstring, UiValue>> values_;
};

}  // namespace ui::application
