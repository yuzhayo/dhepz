#include "modules/contract/module_manifest.h"

#include "core/json.h"

namespace modules {
namespace {

bool IsSlug(const std::wstring& text) {
  if (text.empty()) return false;
  bool expect_start = true;
  for (const wchar_t c : text) {
    const bool lower = c >= L'a' && c <= L'z';
    const bool digit = c >= L'0' && c <= L'9';
    if (expect_start) {
      if (!lower && !digit) return false;
      expect_start = false;
    } else if (c == L'-') {
      expect_start = true;
    } else if (!lower && !digit) {
      return false;
    }
  }
  return !expect_start;  // no trailing dash
}

void Add(std::vector<ManifestDiagnostic>* diags, const json::Value& at, std::wstring message) {
  diags->push_back({std::move(message), at.line(), at.column()});
}

bool StringArray(const json::Value& value, std::vector<std::wstring>* out,
                 std::vector<ManifestDiagnostic>* diags, const wchar_t* name) {
  if (!value.is_array()) {
    Add(diags, value, std::wstring(name) + L" must be an array of strings");
    return false;
  }
  bool ok = true;
  for (const json::Value& item : value.items()) {
    if (!item.is_string()) {
      Add(diags, item, std::wstring(name) + L" must be an array of strings");
      ok = false;
      continue;
    }
    out->push_back(item.AsString());
  }
  return ok;
}

}  // namespace

core::Status ParseManifest(std::wstring_view json_text, ModuleManifest* out,
                           std::vector<ManifestDiagnostic>* diagnostics) {
  std::vector<ManifestDiagnostic> local;
  std::vector<ManifestDiagnostic>& diags = diagnostics != nullptr ? *diagnostics : local;

  json::Value document;
  const core::Status parsed = json::Parse(json_text, &document);
  if (!parsed.ok()) {
    diags.push_back({L"module.json: " + parsed.Message(), 1, 1});
    return core::Err(core::ErrorCode::ParseError, L"module.json does not parse");
  }
  if (!document.is_object()) {
    Add(&diags, document, L"module.json must be an object");
    return core::Err(core::ErrorCode::InvalidArgument, L"module.json invalid");
  }

  ModuleManifest manifest;
  for (const auto& [key, value] : document.members()) {
    if (key == L"moduleId") {
      if (!value.is_string() || !IsSlug(value.AsString())) {
        Add(&diags, value, L"moduleId must be a slug string (a-z0-9 and dashes)");
      } else {
        manifest.module_id = value.AsString();
      }
    } else if (key == L"tabLabel") {
      if (!value.is_string()) {
        Add(&diags, value, L"tabLabel must be a string");
      } else {
        manifest.tab_label = value.AsString();
      }
    } else if (key == L"order") {
      if (!value.is_number()) {
        Add(&diags, value, L"order must be an integer");
      } else {
        manifest.order = static_cast<int>(value.AsNumber());
      }
    } else if (key == L"showInTabs") {
      if (!value.is_bool()) {
        Add(&diags, value, L"showInTabs must be a bool");
      } else {
        manifest.show_in_tabs = value.AsBool();
      }
    } else if (key == L"settingsRoute") {
      if (!value.is_string()) {
        Add(&diags, value, L"settingsRoute must be a string");
      } else {
        manifest.settings_route = value.AsString();
      }
    } else if (key == L"actions") {
      StringArray(value, &manifest.actions, &diags, L"actions");
    } else if (key == L"bindings") {
      StringArray(value, &manifest.bindings, &diags, L"bindings");
    } else if (key == L"capabilities") {
      std::vector<std::wstring> caps;
      if (StringArray(value, &caps, &diags, L"capabilities")) {
        for (const std::wstring& cap : caps) {
          if (cap != std::wstring(kCapabilitySettingsAll) &&
              cap != std::wstring(kCapabilityConfigWrite)) {
            Add(&diags, value, L"unknown capability '" + cap + L"'");
          }
        }
        manifest.capabilities = std::move(caps);
      }
    } else {
      Add(&diags, value, L"unknown manifest field '" + key + L"'");
    }
  }

  if (manifest.module_id.empty()) {
    Add(&diags, document, L"module.json is missing required 'moduleId'");
  }
  if (manifest.tab_label.empty()) {
    Add(&diags, document, L"module.json is missing required 'tabLabel'");
  }

  if (!diags.empty()) {
    return core::Err(core::ErrorCode::InvalidArgument,
                     L"module.json has " + std::to_wstring(diags.size()) + L" diagnostic(s)");
  }
  if (out != nullptr) *out = std::move(manifest);
  return core::Ok();
}

}  // namespace modules
