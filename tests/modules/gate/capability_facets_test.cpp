#include "modules/gate/app_gate.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "framework/test_case.h"
#include "modules/registry/module_registry.h"
#include "platform/files.h"
#include "platform/worker.h"

namespace {

modules::SettingsAllFacet* g_settings_facet = nullptr;
modules::ConfigWriteFacet* g_config_facet = nullptr;
modules::ModuleHost* g_alpha_host = nullptr;

class SettingsModule final : public modules::ModuleDescriptor {
 public:
  std::wstring_view ModuleId() const override { return L"settings"; }
  std::wstring_view TabLabel() const override { return L"Settings"; }
  int Order() const override { return 10; }
  bool ShowInTabs() const override { return true; }
  std::wstring_view SettingsRoute() const override { return {}; }
  std::vector<std::wstring> DeclaredActions() const override { return {}; }
  std::vector<std::wstring> DeclaredBindings() const override { return {}; }
  std::vector<std::wstring> DeclaredCapabilities() const override {
    return {std::wstring(modules::kCapabilitySettingsAll)};
  }
  core::Status Bind(modules::ModuleHost& host) override {
    return host.GetSettingsAllFacet(&g_settings_facet);
  }
  core::Status Handle(std::wstring_view, const json::Value&, json::Value*) override {
    return core::Ok();
  }
  void Release() override {}
};

class EditorModule final : public modules::ModuleDescriptor {
 public:
  std::wstring_view ModuleId() const override { return L"ui-editor"; }
  std::wstring_view TabLabel() const override { return L"UI Editor"; }
  int Order() const override { return 20; }
  bool ShowInTabs() const override { return true; }
  std::wstring_view SettingsRoute() const override { return {}; }
  std::vector<std::wstring> DeclaredActions() const override { return {}; }
  std::vector<std::wstring> DeclaredBindings() const override { return {}; }
  std::vector<std::wstring> DeclaredCapabilities() const override {
    return {std::wstring(modules::kCapabilityConfigWrite)};
  }
  core::Status Bind(modules::ModuleHost& host) override {
    return host.GetConfigWriteFacet(&g_config_facet);
  }
  core::Status Handle(std::wstring_view, const json::Value&, json::Value*) override {
    return core::Ok();
  }
  void Release() override {}
};

class AlphaModule final : public modules::ModuleDescriptor {
 public:
  std::wstring_view ModuleId() const override { return L"alpha"; }
  std::wstring_view TabLabel() const override { return L"Alpha"; }
  int Order() const override { return 30; }
  bool ShowInTabs() const override { return true; }
  std::wstring_view SettingsRoute() const override { return {}; }
  std::vector<std::wstring> DeclaredActions() const override { return {}; }
  std::vector<std::wstring> DeclaredBindings() const override { return {}; }
  std::vector<std::wstring> DeclaredCapabilities() const override { return {}; }
  core::Status Bind(modules::ModuleHost& host) override {
    g_alpha_host = &host;
    return core::Ok();
  }
  core::Status Handle(std::wstring_view, const json::Value&, json::Value*) override {
    return core::Ok();
  }
  void Release() override {}
};

class IntruderModule final : public modules::ModuleDescriptor {
 public:
  std::wstring_view ModuleId() const override { return L"intruder"; }
  std::wstring_view TabLabel() const override { return L"Intruder"; }
  int Order() const override { return 40; }
  bool ShowInTabs() const override { return true; }
  std::wstring_view SettingsRoute() const override { return {}; }
  std::vector<std::wstring> DeclaredActions() const override { return {}; }
  std::vector<std::wstring> DeclaredBindings() const override { return {}; }
  std::vector<std::wstring> DeclaredCapabilities() const override {
    return {std::wstring(modules::kCapabilitySettingsAll)};
  }
  core::Status Bind(modules::ModuleHost&) override { return core::Ok(); }
  core::Status Handle(std::wstring_view, const json::Value&, json::Value*) override {
    return core::Ok();
  }
  void Release() override {}
};

std::unique_ptr<modules::ModuleDescriptor> MakeSettings() {
  return std::make_unique<SettingsModule>();
}

std::unique_ptr<modules::ModuleDescriptor> MakeEditor() {
  return std::make_unique<EditorModule>();
}
std::unique_ptr<modules::ModuleDescriptor> MakeAlpha() {
  return std::make_unique<AlphaModule>();
}
std::unique_ptr<modules::ModuleDescriptor> MakeIntruder() {
  return std::make_unique<IntruderModule>();
}

std::wstring Core() {
  return LR"({
    "schema": "dhepz.ui.core", "version": 1,
    "tokens": { "dark": { "text": "#ffffff" }, "light": { "text": "#000000" } },
    "allows_children": ["screen"],
    "components": {
      "screen": { "properties": {
        "route_id": { "kind": "string", "required": true },
        "module_id": { "kind": "string" },
        "tab_label": { "kind": "string" }
      } }
    }
  })";
}

std::wstring Screen(std::wstring_view id, std::wstring_view label) {
  return L"{ \"type\": \"screen\", \"route_id\": \"" + std::wstring(id) +
         L"-home\", \"module_id\": \"" + std::wstring(id) + L"\", \"tab_label\": \"" +
         std::wstring(label) + L"\" }";
}

std::wstring Manifest(std::wstring_view id, std::wstring_view label,
                      std::wstring_view capability = {}) {
  const std::wstring caps = capability.empty()
                                ? L"[]"
                                : L"[ \"" + std::wstring(capability) + L"\" ]";
  return L"{ \"moduleId\": \"" + std::wstring(id) + L"\", \"tabLabel\": \"" +
         std::wstring(label) + L"\", \"capabilities\": " + caps + L" }";
}

std::wstring Embedded(const std::vector<std::wstring>& screens,
                      const std::vector<std::wstring>& manifests) {
  std::wstring result = L"{ \"core\": " + Core() + L", \"components\": [";
  for (std::size_t i = 0; i < screens.size(); ++i) {
    if (i != 0) result.append(L",");
    result.append(screens[i]);
  }
  result.append(L"], \"modules\": [");
  for (std::size_t i = 0; i < manifests.size(); ++i) {
    if (i != 0) result.append(L",");
    result.append(manifests[i]);
  }
  result.append(L"] }");
  return result;
}

struct RigState {
  unsigned int completion_message = 0;
};
RigState* g_rig = nullptr;

LRESULT CALLBACK FacetRigProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  if (g_rig != nullptr && message == g_rig->completion_message) {
    worker::Worker::Settle(lparam);
    return 0;
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

struct Rig {
  HWND window = nullptr;
  RigState state;
  Rig() {
    g_rig = &state;
    WNDCLASSW cls{};
    cls.lpfnWndProc = FacetRigProc;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = L"dhepz.capability-facet.test";
    RegisterClassW(&cls);
    state.completion_message = RegisterWindowMessageW(L"dhepz.capability-facet.completion");
    window = CreateWindowExW(0, cls.lpszClassName, L"", WS_POPUP, 0, 0, 16, 16, nullptr,
                             nullptr, cls.hInstance, nullptr);
    DHEPZ_CHECK(window != nullptr);
  }
  ~Rig() {
    if (window != nullptr) DestroyWindow(window);
    g_rig = nullptr;
  }
};

template <typename Predicate>
void PumpUntil(Predicate predicate, int timeout_ms) {
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    if (predicate()) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

std::wstring TempFile() {
  wchar_t base[MAX_PATH]{};
  GetTempPathW(MAX_PATH, base);
  return std::wstring(base) + L"dhepz-config-preview-" +
         std::to_wstring(GetTickCount64()) + L".json";
}

void ResetGlobals() {
  g_settings_facet = nullptr;
  g_config_facet = nullptr;
  g_alpha_host = nullptr;
}

}  // namespace

DHEPZ_TEST(SettingsPersistence, NarrowHostSurvivesNewGateOnSameFile) {
  Rig rig;
  const std::wstring path = TempFile();
  modules::ResetRegistryForTests();
  modules::RegisterModule(L"alpha", &MakeAlpha);
  const std::wstring embedded =
      Embedded({Screen(L"alpha", L"Alpha")}, {Manifest(L"alpha", L"Alpha")});

  {
    ResetGlobals();
    modules::AppGate first;
    DHEPZ_CHECK(
        first.ConfigureHostOperations(rig.window, rig.state.completion_message, {}).ok());
    DHEPZ_CHECK(first.ConfigureSettingsStorePath(path).ok());
    DHEPZ_CHECK(first.StartWithEmbedded(embedded).ok());
    DHEPZ_CHECK(first.Activate(L"alpha-home").ok());
    std::atomic<bool> ready{false};
    modules::AsyncRequestToken load;
    DHEPZ_CHECK(g_alpha_host
                    ->StartSettingsLoad(
                        [&](const modules::HostOperationCompletion&) { ready = true; },
                        &load)
                    .ok());
    PumpUntil([&] { return ready.load(); }, 2000);
    DHEPZ_CHECK(ready.load());
    DHEPZ_CHECK(g_alpha_host->SettingsWrite(L"persisted", L"across-process").ok());
  }

  {
    ResetGlobals();
    modules::AppGate second;
    DHEPZ_CHECK(
        second.ConfigureHostOperations(rig.window, rig.state.completion_message, {}).ok());
    DHEPZ_CHECK(second.ConfigureSettingsStorePath(path).ok());
    DHEPZ_CHECK(second.StartWithEmbedded(embedded).ok());
    DHEPZ_CHECK(second.Activate(L"alpha-home").ok());
    std::atomic<bool> ready{false};
    modules::HostOperationCompletion load_completion;
    modules::AsyncRequestToken load;
    DHEPZ_CHECK(g_alpha_host
                    ->StartSettingsLoad(
                        [&](const modules::HostOperationCompletion& completion) {
                          load_completion = completion;
                          ready = true;
                        },
                        &load)
                    .ok());
    PumpUntil([&] { return ready.load(); }, 2000);
    DHEPZ_CHECK(ready.load());
    DHEPZ_CHECK(load_completion.status.ok());
    std::wstring value;
    DHEPZ_CHECK(g_alpha_host->SettingsRead(L"persisted", &value).ok());
    DHEPZ_CHECK_EQ(value, std::wstring(L"across-process"));
  }
  modules::ResetRegistryForTests();
}

DHEPZ_TEST(CapabilityFacets, SettingsAllIsTypedSharedAndRestrictedToSettingsModule) {
  Rig rig;
  modules::ResetRegistryForTests();
  ResetGlobals();
  modules::RegisterModule(L"settings", &MakeSettings);
  modules::RegisterModule(L"alpha", &MakeAlpha);
  modules::RegisterModule(L"intruder", &MakeIntruder);
  modules::AppGate gate;
  DHEPZ_CHECK(gate.ConfigureHostOperations(rig.window, rig.state.completion_message, {}).ok());
  DHEPZ_CHECK(gate.ConfigureSettingsStorePath(TempFile()).ok());
  const std::wstring embedded = Embedded(
      {Screen(L"settings", L"Settings"), Screen(L"alpha", L"Alpha"),
       Screen(L"intruder", L"Intruder")},
      {Manifest(L"settings", L"Settings", modules::kCapabilitySettingsAll),
       Manifest(L"alpha", L"Alpha"),
       Manifest(L"intruder", L"Intruder", modules::kCapabilitySettingsAll)});
  DHEPZ_CHECK(gate.StartWithEmbedded(embedded).ok());
  DHEPZ_CHECK(gate.Mounted(L"settings"));
  DHEPZ_CHECK(gate.Mounted(L"alpha"));
  DHEPZ_CHECK(!gate.Mounted(L"intruder"));
  DHEPZ_CHECK_EQ(gate.Grants().size(), static_cast<std::size_t>(1));
  DHEPZ_CHECK_EQ(gate.Grants()[0].module_id, std::wstring(L"settings"));
  DHEPZ_CHECK_EQ(gate.Grants()[0].capability,
                 std::wstring(modules::kCapabilitySettingsAll));
  DHEPZ_CHECK_EQ(gate.Rejects().size(), static_cast<std::size_t>(1));
  DHEPZ_CHECK(gate.Rejects()[0].reason.find(L"settings:all") != std::wstring::npos);
  DHEPZ_CHECK(!gate.Rejects()[0].file.empty());
  DHEPZ_CHECK(gate.Rejects()[0].line > 0);

  DHEPZ_CHECK(gate.Activate(L"settings-home").ok());
  DHEPZ_CHECK(gate.Activate(L"alpha-home").ok());
  DHEPZ_CHECK(g_settings_facet != nullptr);
  DHEPZ_CHECK(g_alpha_host != nullptr);
  std::atomic<bool> settings_ready{false};
  modules::AsyncRequestToken settings_load;
  DHEPZ_CHECK(g_alpha_host
                  ->StartSettingsLoad(
                      [&](const modules::HostOperationCompletion& completion) {
                        DHEPZ_CHECK(completion.kind ==
                                    modules::HostOperationKind::SettingsLoad);
                        settings_ready = true;
                      },
                      &settings_load)
                  .ok());
  PumpUntil([&] { return settings_ready.load(); }, 2000);
  DHEPZ_CHECK(settings_ready.load());
  modules::SettingsAllFacet* denied_settings = g_settings_facet;
  modules::ConfigWriteFacet* denied_config =
      reinterpret_cast<modules::ConfigWriteFacet*>(g_settings_facet);
  DHEPZ_CHECK(!g_alpha_host->GetSettingsAllFacet(&denied_settings).ok());
  DHEPZ_CHECK(denied_settings == nullptr);
  DHEPZ_CHECK(!g_alpha_host->GetConfigWriteFacet(&denied_config).ok());
  DHEPZ_CHECK(denied_config == nullptr);
  DHEPZ_CHECK(g_settings_facet->WriteGlobal(L"theme", L"dark").ok());
  DHEPZ_CHECK(g_settings_facet->WriteModule(L"alpha", L"value", L"shared").ok());
  std::wstring value;
  DHEPZ_CHECK(g_alpha_host->SettingsReadGlobal(L"theme", &value).ok());
  DHEPZ_CHECK_EQ(value, std::wstring(L"dark"));
  DHEPZ_CHECK(g_alpha_host->SettingsRead(L"value", &value).ok());
  DHEPZ_CHECK_EQ(value, std::wstring(L"shared"));
  DHEPZ_CHECK_EQ(g_settings_facet->Peers().size(), static_cast<std::size_t>(2));
  modules::ResetRegistryForTests();
}

DHEPZ_TEST(CapabilityFacets, ConfigPreviewRejectsInvalidAndUsesRevisionedSaveDiscard) {
  Rig rig;
  modules::ResetRegistryForTests();
  ResetGlobals();
  modules::RegisterModule(L"ui-editor", &MakeEditor);
  modules::AppGate gate;
  const std::wstring path = TempFile();
  DHEPZ_CHECK(gate.ConfigureHostOperations(rig.window, rig.state.completion_message, {}).ok());
  DHEPZ_CHECK(gate.ConfigureConfigOverridePath(path).ok());
  DHEPZ_CHECK(gate.StartWithEmbedded(
                  Embedded({Screen(L"ui-editor", L"UI Editor")},
                           {Manifest(L"ui-editor", L"UI Editor",
                                     modules::kCapabilityConfigWrite)}))
                  .ok());
  DHEPZ_CHECK(gate.Activate(L"ui-editor-home").ok());
  DHEPZ_CHECK(g_config_facet != nullptr);
  DHEPZ_CHECK_EQ(gate.Grants().size(), static_cast<std::size_t>(1));
  DHEPZ_CHECK_EQ(gate.Grants()[0].capability,
                 std::wstring(modules::kCapabilityConfigWrite));

  const ui::config::ResolvedUiDocument* original = gate.document();
  const std::uint64_t original_generation = gate.document_generation();
  modules::ConfigPreviewResult invalid;
  const core::Status invalid_status = g_config_facet->Preview(
      LR"({
        "components": [
          { "type": "screen", "route_id": "ui-editor-home",
            "module_id": "ui-editor", "unknown": true }
        ]
      })",
      &invalid);
  DHEPZ_CHECK(!invalid_status.ok());
  DHEPZ_CHECK(!invalid.diagnostics.empty());
  DHEPZ_CHECK(invalid.diagnostics[0].line > 0);
  DHEPZ_CHECK(gate.document() == original);
  DHEPZ_CHECK_EQ(gate.document_generation(), original_generation);

  const std::wstring candidate_a =
      LR"({ "components": [ { "type": "screen", "route_id": "ui-editor-home",
              "module_id": "ui-editor", "tab_label": "Preview A" } ] })";
  modules::ConfigPreviewResult preview_a;
  DHEPZ_CHECK(g_config_facet->Preview(candidate_a, &preview_a).ok());
  DHEPZ_CHECK(preview_a.token);
  DHEPZ_CHECK(gate.document() != original);
  DHEPZ_CHECK_EQ(gate.document()->FindRoute(L"ui-editor-home")->tab_label,
                 std::wstring(L"Preview A"));

  const std::wstring candidate_b =
      LR"({ "components": [ { "type": "screen", "route_id": "ui-editor-home",
              "module_id": "ui-editor", "tab_label": "Preview B" } ] })";
  modules::ConfigPreviewResult preview_b;
  DHEPZ_CHECK(g_config_facet->Preview(candidate_b, &preview_b).ok());
  DHEPZ_CHECK(preview_b.token != preview_a.token);
  modules::AsyncRequestToken stale_request;
  DHEPZ_CHECK(!g_config_facet->Save(preview_a.token, [](const auto&) {}, &stale_request).ok());

  std::vector<std::wstring> discard_routes;
  DHEPZ_CHECK(g_config_facet->Discard(preview_b.token, &discard_routes).ok());
  DHEPZ_CHECK(gate.document() == original);
  DHEPZ_CHECK(!discard_routes.empty());

  modules::ConfigPreviewResult saved_preview;
  DHEPZ_CHECK(g_config_facet->Preview(candidate_b, &saved_preview).ok());
  std::atomic<bool> saved{false};
  modules::HostOperationCompletion save_completion;
  modules::AsyncRequestToken save_request;
  DHEPZ_CHECK(g_config_facet->Save(
                  saved_preview.token,
                  [&](const modules::HostOperationCompletion& completion) {
                    save_completion = completion;
                    saved = true;
                  },
                  &save_request)
                  .ok());
  PumpUntil([&] { return saved.load(); }, 2000);
  DHEPZ_CHECK(saved.load());
  DHEPZ_CHECK(save_completion.status.ok());
  DHEPZ_CHECK(save_completion.kind == modules::HostOperationKind::ConfigSave);
  std::wstring persisted;
  DHEPZ_CHECK(files::ReadText(path, &persisted).ok());
  DHEPZ_CHECK_EQ(persisted, candidate_b);
  modules::ResetRegistryForTests();
}
