#include "parent/ui/config/ui_schema.h"

#include <algorithm>
#include <array>

namespace ui::config {
namespace {

void Add(std::vector<Diagnostic>* out, const json::Value& at, std::wstring message) {
  if (out != nullptr) out->push_back({std::move(message), at.line(), at.column()});
}

const json::Value* CatalogType(const json::Value& core, std::wstring_view type) {
  const json::Value* components = core.ObjectField(L"components");
  return components != nullptr ? components->Find(type) : nullptr;
}

bool IsInteger(const json::Value& value) {
  return value.is_number() &&
         value.AsNumber() == static_cast<double>(static_cast<long long>(value.AsNumber()));
}

bool KindMatches(const json::Value& value, const json::Value& definition) {
  const std::wstring kind = definition.StringField(L"kind");
  if (kind == L"string" || kind == L"text" || kind == L"binding") {
    return value.is_string();
  }
  if (kind == L"bool") return value.is_bool();
  if (kind == L"int") return IsInteger(value);
  if (kind == L"object") return value.is_object();
  if (kind == L"array") return value.is_array();
  if (kind == L"enum") {
    if (!value.is_string()) return false;
    const json::Value* values = definition.ArrayField(L"values");
    if (values == nullptr) return false;
    return std::any_of(values->items().begin(), values->items().end(), [&value](const auto& item) {
      return item.is_string() && item.AsString() == value.AsString();
    });
  }
  return false;
}

const json::Value* PropertyDefinition(const json::Value& core, const json::Value& catalog,
                                      std::wstring_view name) {
  const json::Value* properties = catalog.ObjectField(L"properties");
  const json::Value* definition = properties != nullptr ? properties->Find(name) : nullptr;
  if (definition != nullptr) return definition;
  const json::Value* common = core.ObjectField(L"common");
  const json::Value* common_properties =
      common != nullptr ? common->ObjectField(L"properties") : nullptr;
  return common_properties != nullptr ? common_properties->Find(name) : nullptr;
}

bool AllowsChildren(const json::Value& core, std::wstring_view type) {
  const json::Value* allowed = core.ArrayField(L"allows_children");
  if (allowed == nullptr) return false;
  return std::any_of(allowed->items().begin(), allowed->items().end(), [type](const auto& item) {
    return item.is_string() && item.AsString() == type;
  });
}

void ValidateComponent(const json::Value& core, const json::Value& component,
                       std::vector<Diagnostic>* diagnostics) {
  if (!component.is_object()) {
    Add(diagnostics, component, L"component must be an object");
    return;
  }
  const json::Value* type_value = component.Find(L"type");
  if (type_value == nullptr || !type_value->is_string()) {
    Add(diagnostics, component, L"component requires a string type");
    return;
  }
  const std::wstring& type = type_value->AsString();
  const json::Value* catalog = CatalogType(core, type);
  if (catalog == nullptr || !catalog->is_object()) {
    Add(diagnostics, *type_value, L"unknown component type '" + type + L"'");
    return;
  }

  for (const json::Value* definitions :
       {catalog->ObjectField(L"properties"),
        core.ObjectField(L"common") != nullptr
            ? core.ObjectField(L"common")->ObjectField(L"properties")
            : nullptr}) {
    if (definitions == nullptr) continue;
    for (const auto& [name, definition] : definitions->members()) {
      if (definition.is_object() && definition.BoolField(L"required") &&
          component.Find(name) == nullptr) {
        Add(diagnostics, component,
            L"component '" + type + L"' is missing required '" + name + L"'");
      }
    }
  }

  for (const auto& [name, value] : component.members()) {
    if (name == L"type") continue;
    if (name == L"children") {
      if (!AllowsChildren(core, type) || !value.is_array()) {
        Add(diagnostics, value, L"component '" + type + L"' may not use these children");
        continue;
      }
      for (const json::Value& child : value.items()) {
        ValidateComponent(core, child, diagnostics);
      }
      continue;
    }
    const json::Value* definition = PropertyDefinition(core, *catalog, name);
    if (definition == nullptr) {
      Add(diagnostics, value, L"unknown property '" + name + L"' on '" + type + L"'");
    } else if (!KindMatches(value, *definition)) {
      Add(diagnostics, value, L"property '" + name + L"' has the wrong kind");
    }
  }
}

}  // namespace

core::Status ValidateCore(const json::Value& core, std::vector<Diagnostic>* diagnostics) {
  if (diagnostics == nullptr) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument, L"core diagnostics output is required");
  }
  if (!core.is_object() || core.StringField(L"schema") != L"dhepz.ui.core" ||
      core.NumberField(L"version") != 1.0 || core.ObjectField(L"components") == nullptr ||
      core.ObjectField(L"common") == nullptr) {
    Add(diagnostics, core, L"invalid core UI catalog");
  }
  const json::Value* components = core.ObjectField(L"components");
  if (components != nullptr) {
    constexpr std::array<std::wstring_view, 14> required_types{
        L"window",   L"screen", L"container", L"text",   L"button",
        L"input",    L"combo",  L"checkbox",  L"toggle", L"card",
        L"list",     L"scrollbar", L"dialog", L"tabs"};
    for (const std::wstring_view type : required_types) {
      if (components->Find(type) == nullptr) {
        Add(diagnostics, *components, L"core UI catalog is missing component '" +
                                          std::wstring(type) + L"'");
      }
    }
    for (const auto& [type, catalog] : components->members()) {
      const json::Value* properties = catalog.ObjectField(L"properties");
      if (!catalog.is_object() || properties == nullptr) {
        Add(diagnostics, catalog, L"catalog '" + type + L"' requires properties");
        continue;
      }
      for (const auto& [name, definition] : properties->members()) {
        if (!definition.is_object() || definition.StringField(L"kind").empty()) {
          Add(diagnostics, definition, L"catalog property '" + name + L"' is invalid");
          continue;
        }
        const json::Value* default_value = definition.Find(L"default");
        if (default_value != nullptr && !KindMatches(*default_value, definition)) {
          Add(diagnostics, *default_value,
              L"catalog default for '" + type + L"." + name + L"' has the wrong kind");
        }
      }
    }
  }
  const json::Value* common = core.ObjectField(L"common");
  const json::Value* common_properties =
      common != nullptr ? common->ObjectField(L"properties") : nullptr;
  if (common_properties == nullptr) {
    Add(diagnostics, core, L"core UI catalog requires common properties");
  } else {
    for (const auto& [name, definition] : common_properties->members()) {
      if (!definition.is_object() || definition.StringField(L"kind").empty()) {
        Add(diagnostics, definition, L"common property '" + name + L"' is invalid");
        continue;
      }
      const json::Value* default_value = definition.Find(L"default");
      if (default_value != nullptr && !KindMatches(*default_value, definition)) {
        Add(diagnostics, *default_value,
            L"common default for '" + name + L"' has the wrong kind");
      }
    }
  }
  return diagnostics->empty()
             ? core::Ok()
             : DHEPZ_ERR(core::ErrorCode::InvalidArgument, L"core UI catalog is invalid");
}

core::Status ValidateScreen(const json::Value& core, const json::Value& screen,
                            std::vector<Diagnostic>* diagnostics) {
  if (diagnostics == nullptr) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument, L"screen diagnostics output is required");
  }
  const json::Value* components = screen.ArrayField(L"components");
  if (!screen.is_object() || components == nullptr) {
    Add(diagnostics, screen, L"screen document requires a components array");
  } else {
    for (const json::Value& component : components->items()) {
      const std::wstring type = component.StringField(L"type");
      if (type != L"window" && type != L"screen") {
        Add(diagnostics, component, L"top-level UI component must be window or screen");
      }
      ValidateComponent(core, component, diagnostics);
    }
  }
  return diagnostics->empty()
             ? core::Ok()
             : DHEPZ_ERR(core::ErrorCode::InvalidArgument, L"screen document is invalid");
}

}  // namespace ui::config
