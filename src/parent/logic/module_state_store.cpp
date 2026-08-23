#include "parent/logic/module_state_store.h"

#include <algorithm>
#include <utility>

#include "core/json.h"
#include "platform/files.h"

namespace modules {
namespace {

json::Value ToJson(const ui::application::UiValue& value) {
  if (const bool* typed = std::get_if<bool>(&value)) return json::Value::Bool(*typed);
  if (const long long* typed = std::get_if<long long>(&value)) {
    return json::Value::Number(std::to_wstring(*typed), static_cast<double>(*typed));
  }
  if (const std::wstring* typed = std::get_if<std::wstring>(&value)) {
    return json::Value::String(*typed);
  }
  if (const auto* typed = std::get_if<std::vector<std::wstring>>(&value)) {
    json::Value array = json::Value::Array();
    for (const std::wstring& item : *typed) array.Append(json::Value::String(item));
    return array;
  }
  return json::Value::Null();
}

ui::application::UiValue FromJson(const json::Value& value) {
  if (value.is_bool()) return value.AsBool();
  if (value.is_number()) return static_cast<long long>(value.AsNumber());
  if (value.is_string()) return value.AsString();
  if (value.is_array()) {
    std::vector<std::wstring> items;
    for (const json::Value& item : value.items()) {
      if (item.is_string()) items.push_back(item.AsString());
    }
    return items;
  }
  return std::monostate{};
}

}  // namespace

ModuleStateStore::ModuleStateStore(std::wstring path) : path_(std::move(path)) {}

core::Status ModuleStateStore::Load() {
  std::wstring text;
  const core::Status read = files::ReadText(path_, &text);
  if (!read.ok()) {
    if (read.Code() == core::ErrorCode::NotFound) return core::Ok();
    return read;
  }
  json::Value root;
  DHEPZ_RETURN_IF_ERROR(json::Parse(text, &root));
  if (!root.is_object()) {
    return DHEPZ_ERR(core::ErrorCode::ParseError, L"settings.json must contain an object");
  }

  ui::application::UiPatch loaded;
  for (const auto& [path, value] : root.members()) {
    loaded.changes.push_back({path, FromJson(value)});
  }
  std::scoped_lock lock(mutex_);
  state_ = std::move(loaded);
  return core::Ok();
}

ui::application::UiPatch ModuleStateStore::Restore(std::wstring_view prefix) const {
  std::scoped_lock lock(mutex_);
  ui::application::UiPatch restored;
  for (const ui::application::UiChange& change : state_.changes) {
    if (change.path.starts_with(prefix)) restored.changes.push_back(change);
  }
  return restored;
}

core::Status ModuleStateStore::Save(const ui::application::UiPatch& patch) {
  std::scoped_lock lock(mutex_);
  ui::application::UiPatch next = state_;
  for (const ui::application::UiChange& change : patch.changes) {
    const auto found = std::find_if(next.changes.begin(), next.changes.end(),
                                    [&change](const auto& item) {
                                      return item.path == change.path;
                                    });
    if (found == next.changes.end()) {
      next.changes.push_back(change);
    } else {
      found->value = change.value;
    }
  }

  json::Value root = json::Value::Object();
  for (const ui::application::UiChange& change : next.changes) {
    root.Set(change.path, ToJson(change.value));
  }
  DHEPZ_RETURN_IF_ERROR(files::WriteTextAtomic(path_, json::Serialize(root)));
  state_ = std::move(next);
  return core::Ok();
}

}  // namespace modules
