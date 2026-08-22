#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "parent/ui/contracts/ui_contract.h"
#include "parent/ui/contracts/ui_state.h"

namespace ui::application {

class UiActionRegistry final {
 public:
  using Handler = std::function<UiPatch(const UiEvent&, const UiState&)>;

  bool Register(std::wstring action, Handler handler);
  bool Replace(std::wstring_view action, Handler handler);
  UiPatch Dispatch(const UiEvent& event, const UiState& state) const;
  bool Has(std::wstring_view action) const;

 private:
  std::vector<std::pair<std::wstring, Handler>> handlers_;
};

}  // namespace ui::application
