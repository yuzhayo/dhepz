#include "ui/config/ui_schema.h"

#include <algorithm>

namespace ui::config {
namespace {

constexpr std::wstring_view kSchemaName = L"dhepz.ui.core";

const std::vector<std::wstring_view>& Kinds() {
  static const std::vector<std::wstring_view> kinds = {
      L"string", L"text", L"bool", L"int", L"enum", L"object", L"array", L"binding"};
  return kinds;
}

void Add(std::vector<Diagnostic>* out, const json::Value& at, std::wstring message) {
  out->push_back({std::move(message), at.line(), at.column()});
}

bool IsHexColor(const std::wstring& text) {
  if (text.size() != 7 && text.size() != 9) return false;
  if (text[0] != L'#') return false;
  for (std::size_t i = 1; i < text.size(); ++i) {
    const wchar_t c = text[i];
    if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f') || (c >= L'A' && c <= L'F'))) {
      return false;
    }
  }
  return true;
}

bool IsInteger(const json::Value& value) {
  if (!value.is_number()) return false;
  const double number = value.AsNumber();
  return number == static_cast<long long>(number);
}

// Validates one "properties" map of a catalog entry: each definition is an
// object with a known kind; enums carry a non-empty string values array.
void ValidatePropertyDefinitions(const json::Value& properties, std::wstring_view owner,
                                 std::vector<Diagnostic>* out) {
  for (const auto& [name, definition] : properties.members()) {
    if (!definition.is_object()) {
      Add(out, definition, L"catalog '" + std::wstring(owner) + L"." + name +
                               L"': property definition must be an object");
      continue;
    }
    const json::Value* kind = definition.Find(L"kind");
    if (kind == nullptr || !kind->is_string()) {
      Add(out, definition, L"catalog '" + std::wstring(owner) + L"." + name +
                               L"': missing or non-string 'kind'");
      continue;
    }
    const std::wstring& kind_text = kind->AsString();
    if (std::find(Kinds().begin(), Kinds().end(), kind_text) == Kinds().end()) {
      Add(out, *kind, L"catalog '" + std::wstring(owner) + L"." + name + L"': unknown kind '" +
                          kind_text + L"'");
    }
    if (kind_text == L"enum") {
      const json::Value* values = definition.ArrayField(L"values");
      bool valid = values != nullptr && values->size() > 0;
      if (valid) {
        for (const json::Value& item : values->items()) {
          if (!item.is_string()) {
            valid = false;
            break;
          }
        }
      }
      if (!valid) {
        Add(out, definition, L"catalog '" + std::wstring(owner) + L"." + name +
                                 L"': enum requires a non-empty string 'values' array");
      }
    }
    const json::Value* required = definition.Find(L"required");
    if (required != nullptr && !required->is_bool()) {
      Add(out, *required, L"catalog '" + std::wstring(owner) + L"." + name +
                              L"': 'required' must be a bool");
    }
  }
}

const json::Value* CatalogType(const json::Value& core, std::wstring_view type) {
  const json::Value* components = core.ObjectField(L"components");
  if (components == nullptr) return nullptr;
  return components->Find(type);
}

bool KindMatches(const json::Value& value, std::wstring_view kind,
                 const json::Value* definition) {
  if (kind == L"string") return value.is_string();
  if (kind == L"text") return value.is_string();
  if (kind == L"bool") return value.is_bool();
  if (kind == L"int") return IsInteger(value);
  if (kind == L"object") return value.is_object();
  if (kind == L"array") return value.is_array();
  if (kind == L"binding") return value.is_string() || value.is_object();
  if (kind == L"enum") {
    if (!value.is_string()) return false;
    const json::Value* values = definition->ArrayField(L"values");
    if (values == nullptr) return false;
    for (const json::Value& item : values->items()) {
      if (item.is_string() && item.AsString() == value.AsString()) return true;
    }
    return false;
  }
  return true;
}

void ValidateComponent(const json::Value& core, const json::Value& component,
                       std::vector<Diagnostic>* out) {
  if (!component.is_object()) {
    Add(out, component, L"component entry must be an object");
    return;
  }
  const json::Value* type = component.Find(L"type");
  if (type == nullptr || !type->is_string()) {
    Add(out, component, L"component is missing a string 'type'");
    return;
  }
  const json::Value* catalog = CatalogType(core, type->AsString());
  if (catalog == nullptr) {
    Add(out, *type, L"unknown component type '" + type->AsString() + L"'");
    return;
  }

  const json::Value* common = core.ObjectField(L"common");
  const json::Value* common_properties =
      common != nullptr ? common->ObjectField(L"properties") : nullptr;
  const json::Value* type_properties = catalog->ObjectField(L"properties");

  // Required properties must be present.
  for (const json::Value* properties : {type_properties, common_properties}) {
    if (properties == nullptr) continue;
    for (const auto& [name, definition] : properties->members()) {
      if (definition.is_object() && definition.BoolField(L"required") &&
          component.Find(name) == nullptr) {
        Add(out, component, L"component '" + type->AsString() + L"' is missing required '" +
                                name + L"'");
      }
    }
  }

  for (const auto& [name, value] : component.members()) {
    if (name == L"type") continue;
    if (name == L"children") {
      const json::Value* allowed = core.ArrayField(L"allows_children");
      bool may = false;
      if (allowed != nullptr) {
        for (const json::Value& item : allowed->items()) {
          if (item.is_string() && item.AsString() == type->AsString()) {
            may = true;
            break;
          }
        }
      }
      if (!may) {
        Add(out, value, L"component '" + type->AsString() + L"' may not have children");
        continue;
      }
      if (!value.is_array()) {
        Add(out, value, L"'children' must be an array");
        continue;
      }
      for (const json::Value& child : value.items()) {
        ValidateComponent(core, child, out);
      }
      continue;
    }
    const json::Value* definition = nullptr;
    if (type_properties != nullptr) definition = type_properties->Find(name);
    if (definition == nullptr && common_properties != nullptr) {
      definition = common_properties->Find(name);
    }
    if (definition == nullptr) {
      Add(out, value, L"unknown property '" + name + L"' on component '" + type->AsString() +
                          L"'");
      continue;
    }
    const std::wstring kind = definition->StringField(L"kind");
    if (!KindMatches(value, kind, definition)) {
      Add(out, value, L"property '" + name + L"' on component '" + type->AsString() +
                          L"' must be of kind '" + kind + L"'");
    }
  }
}

}  // namespace

core::Status ValidateCore(const json::Value& core, std::vector<Diagnostic>* diagnostics) {
  std::vector<Diagnostic> local;
  std::vector<Diagnostic>& out = diagnostics != nullptr ? *diagnostics : local;

  if (!core.is_object()) {
    Add(&out, core, L"core document must be an object");
  } else {
    const json::Value* schema = core.Find(L"schema");
    if (schema == nullptr || !schema->is_string() || schema->AsString() != kSchemaName) {
      Add(&out, core, L"core document 'schema' must be \"" + std::wstring(kSchemaName) + L"\"");
    }
    const json::Value* version = core.Find(L"version");
    if (version == nullptr || !IsInteger(*version)) {
      Add(&out, core, L"core document 'version' must be an integer");
    }
    const json::Value* tokens = core.ObjectField(L"tokens");
    if (tokens == nullptr) {
      Add(&out, core, L"core document is missing the 'tokens' object");
    } else {
      for (const auto& [theme_name, theme] : tokens->members()) {
        if (!theme.is_object()) {
          Add(&out, theme, L"token theme '" + theme_name + L"' must be an object");
          continue;
        }
        for (const auto& [token_name, color] : theme.members()) {
          if (!color.is_string() || !IsHexColor(color.AsString())) {
            Add(&out, color, L"token '" + theme_name + L"." + token_name +
                                 L"' must be a #RRGGBB or #RRGGBBAA colour");
          }
        }
      }
      for (const std::wstring_view required_theme : {L"dark", L"light"}) {
        if (tokens->Find(required_theme) == nullptr) {
          Add(&out, *tokens, L"tokens is missing the '" + std::wstring(required_theme) +
                                 L"' theme");
        }
      }
    }
    const json::Value* components = core.ObjectField(L"components");
    if (components == nullptr || components->members().empty()) {
      Add(&out, core, L"core document is missing the 'components' catalog");
    } else {
      for (const auto& [type_name, entry] : components->members()) {
        if (!entry.is_object()) {
          Add(&out, entry, L"catalog entry '" + type_name + L"' must be an object");
          continue;
        }
        const json::Value* properties = entry.ObjectField(L"properties");
        if (properties == nullptr) {
          Add(&out, entry, L"catalog entry '" + type_name + L"' is missing 'properties'");
          continue;
        }
        ValidatePropertyDefinitions(*properties, type_name, &out);
      }
    }
    const json::Value* common = core.ObjectField(L"common");
    if (common != nullptr) {
      const json::Value* properties = common->ObjectField(L"properties");
      if (properties != nullptr) {
        ValidatePropertyDefinitions(*properties, L"common", &out);
      }
    }
    const json::Value* allowed = core.ArrayField(L"allows_children");
    if (allowed != nullptr) {
      for (const json::Value& item : allowed->items()) {
        if (!item.is_string() || CatalogType(core, item.AsString()) == nullptr) {
          Add(&out, item, L"allows_children must name catalog component types");
        }
      }
    }
  }

  if (!out.empty()) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                     L"core document has " + std::to_wstring(out.size()) + L" diagnostic(s)");
  }
  return core::Ok();
}

core::Status ValidateScreen(const json::Value& core, const json::Value& screen,
                            std::vector<Diagnostic>* diagnostics) {
  std::vector<Diagnostic> local;
  std::vector<Diagnostic>& out = diagnostics != nullptr ? *diagnostics : local;

  if (!screen.is_object()) {
    Add(&out, screen, L"screen document must be an object");
  } else {
    const json::Value* components = screen.ArrayField(L"components");
    if (components == nullptr) {
      Add(&out, screen, L"screen document is missing the 'components' array");
    } else {
      for (const json::Value& component : components->items()) {
        ValidateComponent(core, component, &out);
      }
    }
  }

  if (!out.empty()) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                     L"screen document has " + std::to_wstring(out.size()) +
                         L" diagnostic(s)");
  }
  return core::Ok();
}

}  // namespace ui::config
