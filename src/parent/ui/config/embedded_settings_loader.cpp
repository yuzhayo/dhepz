#include "parent/ui/config/embedded_settings_loader.h"

#include <windows.h>

#include <string>

#include "core/json.h"
#include "platform/strings.h"
#include "resource.h"

namespace ui::config {
namespace {

core::Status ReadResource(HMODULE module, int id, std::wstring* text) {
  if (module == nullptr || text == nullptr) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument, L"embedded UI resource arguments invalid");
  }
  const HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(id), RT_RCDATA);
  if (resource == nullptr) {
    return DHEPZ_ERR(core::ErrorCode::NotFound, L"required core UI resource is missing");
  }
  const DWORD size = SizeofResource(module, resource);
  const HGLOBAL loaded = LoadResource(module, resource);
  const void* bytes = loaded != nullptr ? LockResource(loaded) : nullptr;
  if (bytes == nullptr || size == 0) {
    return DHEPZ_ERR(core::ErrorCode::IoError, L"required core UI resource is empty");
  }
  return str::FromUtf8(
      std::string_view(static_cast<const char*>(bytes), static_cast<std::size_t>(size)), text);
}

}  // namespace

core::Status LoadEmbeddedSettingsDocument(
    void* module_handle, std::vector<Diagnostic>* diagnostics,
    std::unique_ptr<ResolvedUiDocument>* document) {
  HMODULE module = static_cast<HMODULE>(module_handle);
  std::wstring core_text;
  std::wstring settings_text;
  DHEPZ_RETURN_IF_ERROR(ReadResource(module, IDR_UI_CORE_JSON, &core_text));
  DHEPZ_RETURN_IF_ERROR(ReadResource(module, IDR_UI_SETTINGS_JSON, &settings_text));
  json::Value core_value;
  DHEPZ_RETURN_IF_ERROR(json::Parse(core_text, &core_value));
  return ResolveDocument(core_value, {{L"embedded/settings.json", std::move(settings_text)}},
                         diagnostics, document);
}

core::Status LoadEmbeddedFeatureDocument(
    void* module_handle, const std::vector<EmbeddedScreenResource>& resources,
    std::vector<Diagnostic>* diagnostics,
    std::unique_ptr<ResolvedUiDocument>* document) {
  if (resources.empty()) {
    return DHEPZ_ERR(core::ErrorCode::NotFound, L"No feature UI resource is registered");
  }
  HMODULE module = static_cast<HMODULE>(module_handle);
  std::wstring core_text;
  DHEPZ_RETURN_IF_ERROR(ReadResource(module, IDR_UI_CORE_JSON, &core_text));
  json::Value core_value;
  DHEPZ_RETURN_IF_ERROR(json::Parse(core_text, &core_value));
  std::vector<ScreenSource> sources;
  sources.reserve(resources.size());
  for (const EmbeddedScreenResource& resource : resources) {
    std::wstring text;
    DHEPZ_RETURN_IF_ERROR(ReadResource(module, resource.resource_id, &text));
    sources.push_back({resource.name, std::move(text)});
  }
  return ResolveDocument(core_value, sources, diagnostics, document);
}

}  // namespace ui::config
