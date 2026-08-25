#include "parent/ui/runtime/route_tabs.h"

#include <algorithm>
#include <utility>

#include "core/json.h"
#include "platform/files.h"

namespace ui::tabs {

RouteTabs::RouteTabs(std::wstring state_path) : state_path_(std::move(state_path)) {}

core::Status RouteTabs::Load() {
  saved_order_.clear();
  std::wstring text;
  const core::Status read = files::ReadText(state_path_, &text);
  if (!read.ok()) {
    return read.Code() == core::ErrorCode::NotFound ? core::Ok() : read;
  }

  json::Value root;
  DHEPZ_RETURN_IF_ERROR(json::Parse(text, &root));
  if (!root.is_object()) {
    return DHEPZ_ERR(core::ErrorCode::ParseError, L"tabs.json must contain an object");
  }

  if (const json::Value* order = root.ArrayField(L"order")) {
    for (const json::Value& item : order->items()) {
      if (item.is_string() && !item.AsString().empty() &&
          std::find(saved_order_.begin(), saved_order_.end(), item.AsString()) ==
              saved_order_.end()) {
        saved_order_.push_back(item.AsString());
      }
    }
  }
  locked_ = root.BoolField(L"locked", false);
  multi_row_ = root.BoolField(L"multi_row", true);
  return core::Ok();
}

void RouteTabs::Resolve(const config::ResolvedUiDocument& document) {
  std::vector<std::pair<std::wstring, std::wstring>> available;
  for (const config::Route& route : document.routes()) {
    if (route.show_in_tabs) available.emplace_back(route.id, route.tab_label);
  }

  order_.clear();
  labels_.clear();
  const auto append = [&](std::wstring_view id) {
    const auto found = std::find_if(available.begin(), available.end(), [id](const auto& item) {
      return item.first == id;
    });
    if (found == available.end() ||
        std::find(order_.begin(), order_.end(), found->first) != order_.end()) {
      return;
    }
    order_.push_back(found->first);
    labels_.push_back(found->second.empty() ? found->first : found->second);
  };
  for (const std::wstring& id : saved_order_) append(id);
  for (const auto& [id, label] : available) {
    (void)label;
    append(id);
  }
  saved_order_ = order_;
}

application::UiPatch RouteTabs::Patch(std::wstring_view active_route) const {
  application::UiPatch patch;
  patch.changes = {
      {L"parent.tabs.routes", order_},
      {L"parent.tabs.labels", labels_},
      {L"parent.tabs.selected", std::wstring(active_route)},
      {L"parent.tabs.locked", locked_},
      {L"parent.tabs.multi_row", multi_row_},
  };
  return patch;
}

application::UiPatch RouteTabs::Select(std::wstring_view route) const {
  application::UiPatch patch;
  if (std::find(order_.begin(), order_.end(), route) == order_.end()) return patch;
  patch.route = std::wstring(route);
  patch.changes.push_back({L"parent.tabs.selected", std::wstring(route)});
  return patch;
}

bool RouteTabs::Reorder(std::size_t from, std::size_t to) {
  if (locked_ || from >= order_.size() || to >= order_.size() || from == to) return false;
  const std::wstring route = order_[from];
  const std::wstring label = labels_[from];
  order_.erase(order_.begin() + static_cast<std::ptrdiff_t>(from));
  labels_.erase(labels_.begin() + static_cast<std::ptrdiff_t>(from));
  order_.insert(order_.begin() + static_cast<std::ptrdiff_t>(to), route);
  labels_.insert(labels_.begin() + static_cast<std::ptrdiff_t>(to), label);
  saved_order_ = order_;
  return true;
}

bool RouteTabs::SetLocked(bool locked) {
  if (locked_ == locked) return false;
  locked_ = locked;
  return true;
}

bool RouteTabs::SetMultiRow(bool multi_row) {
  if (multi_row_ == multi_row) return false;
  multi_row_ = multi_row;
  return true;
}

std::wstring RouteTabs::Serialize() const {
  json::Value root = json::Value::Object();
  json::Value order = json::Value::Array();
  for (const std::wstring& route : order_) order.Append(json::Value::String(route));
  root.Set(L"order", std::move(order));
  root.Set(L"locked", json::Value::Bool(locked_));
  root.Set(L"multi_row", json::Value::Bool(multi_row_));
  return json::Serialize(root);
}

core::Status RouteTabs::SaveSerialized(std::wstring text) const {
  return files::WriteTextAtomic(state_path_, text);
}

core::Status RouteTabs::Save() const {
  return SaveSerialized(Serialize());
}

}  // namespace ui::tabs
