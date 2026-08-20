#include "ui/presenter/screen_presenter.h"

#include <string>
#include <vector>

#include "framework/test_case.h"

namespace {

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
  void Invalidate(const render::Rect&) override {}
  void InvalidateAll() override {}
  bool HasInvalidation() const override { return false; }
  bool TakeInvalidation(render::Rect&) override { return false; }
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
      "backdrop": { "kind": "string" } } },
    "container": { "properties": { "gap": { "kind": "int", "default": 8 } } },
    "text": { "properties": { "text": { "kind": "text", "required": true } } },
    "button": { "properties": {
      "label": { "kind": "text", "required": true },
      "selected": { "kind": "binding" },
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
