#include "ui/presenter/screen_presenter.h"

#include <algorithm>
#include <string>
#include <vector>

#include "framework/test_case.h"
#include "modules/gate/app_gate.h"
#include "modules/registry/module_registry.h"

namespace {

class BridgeModule final : public modules::ModuleDescriptor {
 public:
  std::wstring_view ModuleId() const override { return L"bridge"; }
  std::wstring_view TabLabel() const override { return L"bridge"; }
  int Order() const override { return 1; }
  bool ShowInTabs() const override { return true; }
  std::wstring_view SettingsRoute() const override { return {}; }
  std::vector<std::wstring> DeclaredActions() const override {
    return {L"bridge:run"};
  }
  std::vector<std::wstring> DeclaredBindings() const override {
    return {L"status"};
  }
  std::vector<std::wstring> DeclaredCapabilities() const override { return {}; }
  core::Status Bind(modules::ModuleHost& host) override {
    host_ = &host;
    return core::Ok();
  }
  core::Status Handle(std::wstring_view action, const json::Value& payload,
                      json::Value* patch) override {
    if (action != L"bridge:run") {
      return core::Err(core::ErrorCode::NotFound, L"unknown bridge action");
    }
    *patch = json::Value::Object();
    patch->Set(L"status", json::Value::String(
        L"Dispatched " + payload.StringField(L"before")));
    return core::Ok();
  }
  void Release() override { host_ = nullptr; }

 private:
  modules::ModuleHost* host_ = nullptr;
};

std::unique_ptr<modules::ModuleDescriptor> MakeBridgeModule() {
  return std::make_unique<BridgeModule>();
}

// Records call order so the z-order (backdrop before content) is assertable.
class RecordingBackend final : public render::RenderBackend {
 public:
  void Resize(render::Size) override {}
  void SetDpi(float) override {}
  float dpi() const override { return 96.0f; }
  render::Size surface_size() const override { return {}; }
  void BeginFrame(const render::Rect&) override {}
  void EndFrame() override {}
  void Present(void*) override {}
  void Invalidate(const render::Rect& rect) override { invalidations.push_back(rect); }
  void InvalidateAll() override { invalidated_all = true; }
  bool HasInvalidation() const override {
    return !invalidations.empty() || invalidated_all;
  }
  bool TakeInvalidation(render::Rect& rect) override {
    if (invalidations.empty()) return false;
    rect = invalidations.front();
    invalidations.erase(invalidations.begin());
    return true;
  }
  render::ImageHandle LoadImageFile(std::wstring_view) override {
    return render::ImageHandle::Invalid;
  }
  void ReleaseImage(render::ImageHandle) override {}
  render::Size MeasureText(std::wstring_view, const render::TextStyle&, float) override {
    return {100.0f, 20.0f};
  }
  void FillRect(const render::Rect&, render::Color) override { calls.push_back(L"fill"); }
  void StrokeRect(const render::Rect&, render::Color, float) override {
    calls.push_back(L"ring");
  }
  void FillRoundedRect(const render::Rect& rect, const render::CornerRadius&, render::Color color) override {
    calls.push_back(L"button");
    fills.push_back({rect, color});
  }
  void StrokeRoundedRect(const render::Rect& rect, const render::CornerRadius&,
                         render::Color color, float stroke_width) override {
    if (stroke_width >= 2.0f) {
      calls.push_back(L"ring");
    } else {
      strokes.push_back({rect, color});
    }
  }
  void DrawTextRun(std::wstring_view text, const render::Rect&, const render::TextStyle&,
                   render::Color, render::TextAlign, render::VerticalAlign) override {
    calls.push_back(L"text:" + std::wstring(text));
  }
  void DrawImage(render::ImageHandle, const render::Rect&, float) override {}
  void PushClip(const render::Rect&) override {}
  void PopClip() override {}
  void PushTranslation(render::Point) override {}
  void PopTranslation() override {}

  std::vector<std::wstring> calls;
  std::vector<std::pair<render::Rect, render::Color>> fills;
  std::vector<std::pair<render::Rect, render::Color>> strokes;
  std::vector<render::Rect> invalidations;
  bool invalidated_all = false;
};

std::wstring W(const char* utf8) {
  std::wstring wide;
  for (const char* p = utf8; *p != '\0'; ++p) {
    wide.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*p)));
  }
  return wide;
}

const char* kCore = R"({
  "schema": "dhepz.ui.core",
  "version": 1,
  "tokens": {
    "dark": { "accent": "#60A5FA", "surfaceAlt": "#232730", "border": "#303540",
              "text": "#E9EDF4", "window": "#14161B" },
    "light": { "accent": "#2563EB", "surfaceAlt": "#F0F2F6", "border": "#D8DCE3",
               "text": "#181C24", "window": "#F5F6F9" }
  },
  "common": { "properties": { "id": { "kind": "string" } } },
  "allows_children": ["screen", "container"],
  "components": {
    "screen": { "properties": {
      "route_id": { "kind": "string", "required": true },
      "module_id": { "kind": "string" },
      "backdrop": { "kind": "string" } } },
    "container": { "properties": { "gap": { "kind": "int", "default": 8 } } },
    "text": { "properties": { "text": { "kind": "text", "required": true } } },
    "button": { "properties": {
      "label": { "kind": "text", "required": true },
      "selected": { "kind": "binding" },
      "action": { "kind": "string" },
      "action_payload": { "kind": "object" },
      "tab_stop": { "kind": "bool", "default": true } } },
    "input": { "properties": {
      "value_binding": { "kind": "binding" },
      "placeholder": { "kind": "string" },
      "maximum_length": { "kind": "int", "default": 4096 },
      "tab_stop": { "kind": "bool", "default": true } } },
    "combo": { "properties": {
      "items_binding": { "kind": "binding" },
      "selected_value_binding": { "kind": "binding" },
      "placeholder": { "kind": "string" },
      "tab_stop": { "kind": "bool", "default": true } } },
    "toggle": { "properties": {
      "label": { "kind": "text" },
      "checked_binding": { "kind": "binding" },
      "tab_stop": { "kind": "bool", "default": true } } }
  }
})";

const char* kScreens = R"({
  "components": [
    { "type": "screen", "route_id": "home", "backdrop": "window", "children": [
      { "type": "container", "children": [
        { "type": "text", "text": "hello" },
        { "type": "button", "id": "go", "label": "Go" }
      ] }
    ] },
    { "type": "screen", "route_id": "other", "children": [
      { "type": "text", "text": "second" }
    ] }
  ]
})";

std::unique_ptr<ui::config::ResolvedUiDocument> Resolve() {
  json::Value core;
  DHEPZ_CHECK(json::Parse(W(kCore), &core).ok());
  std::vector<ui::config::Diagnostic> diagnostics;
  std::unique_ptr<ui::config::ResolvedUiDocument> document;
  DHEPZ_CHECK(ui::config::ResolveDocument(core, {{L"embedded", W(kScreens)}}, &diagnostics,
                                          &document)
                  .ok());
  return document;
}

}  // namespace

DHEPZ_TEST(ScreenPresenter, PaintsBackdropFirstThenContent) {
  RecordingBackend backend;
  ui::presenter::ScreenPresenter presenter(&backend);
  const auto document = Resolve();
  presenter.SetDocument(document.get());

  presenter.Prepare({24.0f, 24.0f, 400.0f, 300.0f});
  presenter.Paint({24.0f, 24.0f, 400.0f, 300.0f});

  DHEPZ_CHECK(!backend.calls.empty());
  DHEPZ_CHECK_EQ(backend.calls[0], std::wstring(L"fill"));  // backdrop window token
  bool saw_text = false;
  for (const auto& call : backend.calls) {
    if (call == L"text:hello") saw_text = true;
  }
  DHEPZ_CHECK(saw_text);
  DHEPZ_CHECK(!backend.strokes.empty());  // the button outline
}

DHEPZ_TEST(ScreenPresenter, TabAdvancesFocusAndDrawsTheRing) {
  RecordingBackend backend;
  ui::presenter::ScreenPresenter presenter(&backend);
  const auto document = Resolve();
  presenter.SetDocument(document.get());
  presenter.Prepare({0.0f, 0.0f, 400.0f, 300.0f});
  presenter.Paint({0.0f, 0.0f, 400.0f, 300.0f});

  DHEPZ_CHECK(presenter.HandleKey(0x09));  // VK_TAB
  DHEPZ_CHECK_EQ(presenter.focused(), std::wstring(L"go"));

  backend.calls.clear();
  presenter.Prepare({0.0f, 0.0f, 400.0f, 300.0f});
  presenter.Paint({0.0f, 0.0f, 400.0f, 300.0f});
  bool saw_ring = false;
  for (const auto& call : backend.calls) {
    if (call == L"ring") saw_ring = true;
  }
  DHEPZ_CHECK(saw_ring);
}

DHEPZ_TEST(ScreenPresenter, ClickFocusesTheHitButton) {
  RecordingBackend backend;
  ui::presenter::ScreenPresenter presenter(&backend);
  const auto document = Resolve();
  presenter.SetDocument(document.get());
  presenter.Prepare({0.0f, 0.0f, 400.0f, 300.0f});
  presenter.Paint({0.0f, 0.0f, 400.0f, 300.0f});

  // Button sits below the text row: container column, gap 8, text 20 high.
  DHEPZ_CHECK(presenter.HandleClick(50.0f, 76.0f));
  DHEPZ_CHECK_EQ(presenter.focused(), std::wstring(L"go"));
}

DHEPZ_TEST(ScreenPresenter, IdleOutlineHoverBrightensPressBumps) {
  RecordingBackend backend;
  ui::presenter::ScreenPresenter presenter(&backend);
  const auto document = Resolve();
  presenter.SetDocument(document.get());
  presenter.Prepare({0.0f, 0.0f, 400.0f, 300.0f});
  presenter.Paint({0.0f, 0.0f, 400.0f, 300.0f});
  DHEPZ_CHECK(!backend.strokes.empty());
  // The only fill is the selected tab's tint; idle buttons stay unfilled.
  DHEPZ_CHECK_EQ(backend.fills.size(), static_cast<std::size_t>(1));
  const render::Color base = backend.strokes.back().second;
  const float base_y = backend.strokes.back().first.y;

  // The button sits below the text row.
  DHEPZ_CHECK(presenter.HandleMove(50.0f, 76.0f));
  backend.strokes.clear();
  presenter.Paint({0.0f, 0.0f, 400.0f, 300.0f});
  DHEPZ_CHECK(backend.strokes.back().second.r > base.r);  // brightened outline
  DHEPZ_CHECK_EQ(backend.strokes.back().first.y, base_y);

  DHEPZ_CHECK(presenter.HandleDown(50.0f, 76.0f));
  backend.strokes.clear();
  presenter.Paint({0.0f, 0.0f, 400.0f, 300.0f});
  DHEPZ_CHECK(backend.strokes.back().first.y > base_y);  // bumped 1px

  // Release clears the press.
  presenter.HandleClick(50.0f, 76.0f);
  backend.strokes.clear();
  presenter.Paint({0.0f, 0.0f, 400.0f, 300.0f});
  DHEPZ_CHECK_EQ(backend.strokes.back().first.y, base_y);
}

DHEPZ_TEST(ScreenPresenter, SelectedButtonGetsAccentOutlineAndTint) {
  RecordingBackend backend;
  ui::presenter::ScreenPresenter presenter(&backend);
  json::Value core;
  DHEPZ_CHECK(json::Parse(W(kCore), &core).ok());
  std::vector<ui::config::Diagnostic> diagnostics;
  std::unique_ptr<ui::config::ResolvedUiDocument> document;
  DHEPZ_CHECK(ui::config::ResolveDocument(
                  core,
                  {{L"embedded",
                    W(R"({
                      "components": [
                        { "type": "screen", "route_id": "home", "children": [
                          { "type": "button", "id": "lit", "label": "Lit",
                            "selected": true }
                        ] }
                      ]
                    })")}},
                  &diagnostics, &document)
                  .ok());
  presenter.SetDocument(document.get());
  presenter.Prepare({0.0f, 0.0f, 400.0f, 300.0f});
  presenter.Paint({0.0f, 0.0f, 400.0f, 300.0f});
  DHEPZ_CHECK(!backend.fills.empty());  // the tint
  DHEPZ_CHECK_EQ(static_cast<int>(backend.strokes.back().second.r), 0x60);  // accent outline
}

DHEPZ_TEST(ScreenPresenter, TabsSwitchRoutesAndLightTheSelectedTab) {
  RecordingBackend backend;
  ui::presenter::ScreenPresenter presenter(&backend);
  const auto document = Resolve();
  presenter.SetDocument(document.get());
  presenter.Prepare({0.0f, 0.0f, 400.0f, 300.0f});
  presenter.Paint({0.0f, 0.0f, 400.0f, 300.0f});

  // Both routes show in tabs; labels painted; first tab selected (accent).
  bool saw_home = false, saw_other = false;
  for (const auto& call : backend.calls) {
    if (call == L"text:home") saw_home = true;
    if (call == L"text:other") saw_other = true;
  }
  DHEPZ_CHECK(saw_home);
  DHEPZ_CHECK(saw_other);
  DHEPZ_CHECK_EQ(static_cast<int>(backend.strokes.front().second.r), 0x60);

  // Click the second tab (x past the first tab's 120px width).
  DHEPZ_CHECK(presenter.HandleClick(188.0f, 14.0f));
  DHEPZ_CHECK_EQ(presenter.current_route(), std::wstring(L"other"));
  backend.calls.clear();
  presenter.Prepare({0.0f, 0.0f, 400.0f, 300.0f});
  presenter.Paint({0.0f, 0.0f, 400.0f, 300.0f});
  bool saw_second = false;
  for (const auto& call : backend.calls) {
    if (call == L"text:second") saw_second = true;
  }
  DHEPZ_CHECK(saw_second);
}

DHEPZ_TEST(ScreenPresenter, RouteSwitchChangesTheDrawSet) {
  RecordingBackend backend;
  ui::presenter::ScreenPresenter presenter(&backend);
  const auto document = Resolve();
  presenter.SetDocument(document.get());

  presenter.SwitchRoute(L"other");
  DHEPZ_CHECK_EQ(presenter.current_route(), std::wstring(L"other"));
  presenter.Prepare({0.0f, 0.0f, 400.0f, 300.0f});
  presenter.Paint({0.0f, 0.0f, 400.0f, 300.0f});
  bool saw_second = false;
  for (const auto& call : backend.calls) {
    if (call == L"text:second") saw_second = true;
  }
  DHEPZ_CHECK(saw_second);
}

DHEPZ_TEST(ScreenPresenter, ActionPayloadResolvesAndImmediatePatchUpdatesBoundText) {
  RecordingBackend backend;
  json::Value core;
  DHEPZ_CHECK(json::Parse(W(kCore), &core).ok());
  std::vector<ui::config::Diagnostic> diagnostics;
  std::unique_ptr<ui::config::ResolvedUiDocument> document;
  DHEPZ_CHECK(ui::config::ResolveDocument(
                  core,
                  {{L"embedded", W(R"({ "components": [
                    { "type": "screen", "route_id": "home", "children": [
                      { "type": "button", "id": "go",
                        "label": { "$bind": "label" },
                        "action": "demo:go",
                        "action_payload": {
                          "literal": 7,
                          "folder": { "$bind": "folder" },
                          "nested": [true, { "$bind": "label" }] }
                      }
                    ] }
                  ] })")}},
                  &diagnostics, &document).ok());

  ui::presenter::ScreenPresenter presenter(&backend);
  presenter.SetDocument(document.get());
  json::Value initial = json::Value::Object();
  initial.Set(L"label", json::Value::String(L"Open"));
  initial.Set(L"folder", json::Value::String(L"C:\\work"));
  DHEPZ_CHECK(presenter.ApplyStatePatch(L"home", initial).ok());

  json::Value captured;
  presenter.set_action_dispatch_handler(
      [&](std::wstring_view route, std::wstring_view action,
          const json::Value& payload, json::Value* state_patch) {
        DHEPZ_CHECK_EQ(std::wstring(route), std::wstring(L"home"));
        DHEPZ_CHECK_EQ(std::wstring(action), std::wstring(L"demo:go"));
        captured = payload;
        *state_patch = json::Value::Object();
        state_patch->Set(L"label", json::Value::String(L"Opened"));
        return core::Ok();
      });
  presenter.Prepare({10.0f, 20.0f, 400.0f, 300.0f});
  backend.invalidations.clear();
  backend.invalidated_all = false;
  DHEPZ_CHECK(presenter.HandleClick(50.0f, 48.0f));

  DHEPZ_CHECK_EQ(captured.NumberField(L"literal"), 7.0);
  DHEPZ_CHECK_EQ(captured.StringField(L"folder"), std::wstring(L"C:\\work"));
  DHEPZ_CHECK_EQ(captured.ArrayField(L"nested")->items()[1].AsString(),
                 std::wstring(L"Open"));
  DHEPZ_CHECK_EQ(presenter.ViewStateValue(L"home", L"label")->AsString(),
                 std::wstring(L"Opened"));
  DHEPZ_CHECK_EQ(backend.invalidations.size(), static_cast<std::size_t>(1));
  DHEPZ_CHECK_FALSE(backend.invalidated_all);
}

DHEPZ_TEST(ScreenPresenter, AsyncPatchInvalidatesOnlyBoundNode) {
  RecordingBackend backend;
  json::Value core;
  DHEPZ_CHECK(json::Parse(W(kCore), &core).ok());
  std::vector<ui::config::Diagnostic> diagnostics;
  std::unique_ptr<ui::config::ResolvedUiDocument> document;
  DHEPZ_CHECK(ui::config::ResolveDocument(
                  core,
                  {{L"embedded", W(R"({ "components": [
                    { "type": "screen", "route_id": "home", "children": [
                      { "type": "text", "text": "static" },
                      { "type": "button", "label": { "$bind": "status" } }
                    ] }
                  ] })")}},
                  &diagnostics, &document).ok());
  ui::presenter::ScreenPresenter presenter(&backend);
  presenter.SetDocument(document.get());
  presenter.Prepare({0.0f, 0.0f, 400.0f, 300.0f});
  backend.invalidated_all = false;

  json::Value patch = json::Value::Object();
  patch.Set(L"status", json::Value::String(L"done"));
  DHEPZ_CHECK(presenter.ApplyStatePatch(L"home", patch).ok());
  DHEPZ_CHECK_EQ(backend.invalidations.size(), static_cast<std::size_t>(1));
  DHEPZ_CHECK(backend.invalidations[0].height < 300.0f);
  DHEPZ_CHECK_FALSE(backend.invalidated_all);
}

DHEPZ_TEST(ScreenPresenter, SyntheticClickTravelsThroughGateAndModulePatch) {
  modules::ResetRegistryForTests();
  modules::RegisterModule(L"bridge", &MakeBridgeModule);
  const std::wstring embedded =
      L"{ \"core\": " + W(kCore) +
      L", \"components\": ["
      L"{ \"type\": \"screen\", \"route_id\": \"bridge\", "
      L"\"module_id\": \"bridge\", \"children\": ["
      L"{ \"type\": \"button\", \"id\": \"run\", "
      L"\"label\": { \"$bind\": \"status\" }, \"action\": \"bridge:run\", "
      L"\"action_payload\": { \"before\": { \"$bind\": \"status\" } } } ] }"
      L"], \"modules\": ["
      L"{ \"moduleId\": \"bridge\", \"tabLabel\": \"bridge\", "
      L"\"order\": 1, \"showInTabs\": true, "
      L"\"actions\": [\"bridge:run\"], \"bindings\": [\"status\"], "
      L"\"capabilities\": [] } ] }";
  modules::AppGate gate;
  DHEPZ_CHECK(gate.StartWithEmbedded(embedded).ok());
  DHEPZ_CHECK(gate.Mounted(L"bridge"));

  RecordingBackend backend;
  ui::presenter::ScreenPresenter presenter(&backend);
  presenter.SetDocument(gate.document());
  json::Value initial = json::Value::Object();
  initial.Set(L"status", json::Value::String(L"Ready"));
  DHEPZ_CHECK(presenter.ApplyStatePatch(L"bridge", initial).ok());
  presenter.set_action_dispatch_handler(
      [&](std::wstring_view route, std::wstring_view action,
          const json::Value& payload, json::Value* state_patch) {
        DHEPZ_RETURN_IF_ERROR(gate.Activate(route));
        return gate.Dispatch(action, payload, state_patch);
      });
  presenter.Prepare({0.0f, 0.0f, 400.0f, 300.0f});
  DHEPZ_CHECK(presenter.HandleClick(50.0f, 48.0f));
  DHEPZ_CHECK_EQ(presenter.ViewStateValue(L"bridge", L"status")->AsString(),
                 std::wstring(L"Dispatched Ready"));
  DHEPZ_CHECK_EQ(gate.Diagnostics().statuses.size(), static_cast<std::size_t>(1));
  DHEPZ_CHECK(gate.Diagnostics().statuses[0].ok);
  modules::ResetRegistryForTests();
}

DHEPZ_TEST(ScreenPresenter, TerminalControlsRenderAndUpdateGenericBindings) {
  RecordingBackend backend;
  json::Value core;
  DHEPZ_CHECK(json::Parse(W(kCore), &core).ok());
  std::vector<ui::config::Diagnostic> diagnostics;
  std::unique_ptr<ui::config::ResolvedUiDocument> document;
  DHEPZ_CHECK(ui::config::ResolveDocument(
                  core,
                  {{L"embedded", W(R"({ "components": [
                    { "type": "screen", "route_id": "home", "children": [
                      { "type": "input", "id": "folder",
                        "value_binding": "folder", "placeholder": "Folder" },
                      { "type": "toggle", "id": "admin", "label": "Admin",
                        "checked_binding": "admin" },
                      { "type": "combo", "id": "distro",
                        "items_binding": "distros",
                        "selected_value_binding": "selected",
                        "placeholder": "Distro" }
                    ] }
                  ] })")}},
                  &diagnostics, &document).ok());
  ui::presenter::ScreenPresenter presenter(&backend);
  presenter.SetDocument(document.get());
  json::Value state = json::Value::Object();
  state.Set(L"folder", json::Value::String(L"C:\\work"));
  state.Set(L"admin", json::Value::Bool(false));
  json::Value distros = json::Value::Array();
  distros.Append(json::Value::String(L"Ubuntu"));
  distros.Append(json::Value::String(L"Debian"));
  state.Set(L"distros", std::move(distros));
  state.Set(L"selected", json::Value::String(L"Ubuntu"));
  DHEPZ_CHECK(presenter.ApplyStatePatch(L"home", state).ok());
  presenter.Prepare({0.0f, 0.0f, 400.0f, 300.0f});
  presenter.Paint({0.0f, 0.0f, 400.0f, 300.0f});

  render::Rect admin{};
  DHEPZ_CHECK(presenter.InteractiveBounds(L"admin", &admin));
  DHEPZ_CHECK(presenter.HandleClick(admin.x + 4.0f, admin.y + 8.0f));
  DHEPZ_CHECK(presenter.ViewStateValue(L"home", L"admin")->AsBool());

  render::Rect distro{};
  DHEPZ_CHECK(presenter.InteractiveBounds(L"distro", &distro));
  DHEPZ_CHECK(presenter.HandleClick(distro.x + 4.0f, distro.y + 8.0f));
  DHEPZ_CHECK_EQ(presenter.ViewStateValue(L"home", L"selected")->AsString(),
                 std::wstring(L"Debian"));
  DHEPZ_CHECK(std::find(backend.calls.begin(), backend.calls.end(),
                        std::wstring(L"text:C:\\work")) != backend.calls.end());
}

DHEPZ_TEST(ScreenPresenter, FocusedInputEditsOnlyItsBoundRouteState) {
  RecordingBackend backend;
  json::Value core;
  DHEPZ_CHECK(json::Parse(W(kCore), &core).ok());
  std::vector<ui::config::Diagnostic> diagnostics;
  std::unique_ptr<ui::config::ResolvedUiDocument> document;
  DHEPZ_CHECK(ui::config::ResolveDocument(
                  core,
                  {{L"embedded", W(R"({ "components": [
                    { "type": "screen", "route_id": "home", "children": [
                      { "type": "input", "id": "folder",
                        "value_binding": "working_folder", "maximum_length": 3 }
                    ] }
                  ] })")}},
                  &diagnostics, &document).ok());
  ui::presenter::ScreenPresenter presenter(&backend);
  presenter.SetDocument(document.get());
  presenter.Prepare({0.0f, 0.0f, 400.0f, 300.0f});
  render::Rect input{};
  DHEPZ_CHECK(presenter.InteractiveBounds(L"folder", &input));
  DHEPZ_CHECK(presenter.HandleClick(input.x + 4.0f, input.y + 8.0f));
  DHEPZ_CHECK(presenter.HandleText(L'a'));
  DHEPZ_CHECK(presenter.HandleText(L'b'));
  DHEPZ_CHECK(presenter.HandleText(L'c'));
  DHEPZ_CHECK(presenter.HandleText(L'd'));  // consumed but capped
  DHEPZ_CHECK_EQ(
      presenter.ViewStateValue(L"home", L"working_folder")->AsString(),
      std::wstring(L"abc"));
  DHEPZ_CHECK(presenter.HandleKey(0x08));  // VK_BACK
  DHEPZ_CHECK_EQ(
      presenter.ViewStateValue(L"home", L"working_folder")->AsString(),
      std::wstring(L"ab"));
}
