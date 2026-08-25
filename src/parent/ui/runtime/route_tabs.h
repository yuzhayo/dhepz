#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "core/status.h"
#include "parent/ui/config/resolved_ui_document.h"
#include "parent/ui/contracts/ui_contract.h"

namespace ui::tabs {

class RouteTabs final {
 public:
  explicit RouteTabs(std::wstring state_path);

  core::Status Load();
  void Resolve(const config::ResolvedUiDocument& document);

  application::UiPatch Patch(std::wstring_view active_route) const;
  application::UiPatch Select(std::wstring_view route) const;

  bool Reorder(std::size_t from, std::size_t to);
  bool SetLocked(bool locked);
  bool SetMultiRow(bool multi_row);
  core::Status Save() const;

  const std::vector<std::wstring>& order() const { return order_; }
  const std::vector<std::wstring>& labels() const { return labels_; }
  bool locked() const { return locked_; }
  bool multi_row() const { return multi_row_; }

 private:
  std::wstring state_path_;
  std::vector<std::wstring> saved_order_;
  std::vector<std::wstring> order_;
  std::vector<std::wstring> labels_;
  bool locked_ = false;
  bool multi_row_ = true;
};

}  // namespace ui::tabs
