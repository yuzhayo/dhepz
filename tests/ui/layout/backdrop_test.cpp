#include "ui/layout/backdrop.h"

#include <string>

#include "framework/test_case.h"

namespace {

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
    ++loads;
    return static_cast<render::ImageHandle>(7);
  }
  void ReleaseImage(render::ImageHandle) override { ++releases; }
  render::Size MeasureText(std::wstring_view, const render::TextStyle&, float) override {
    ++measure_calls;
    return {100.0f, 20.0f};
  }
  void FillRect(const render::Rect&, render::Color color) override {
    ++fills;
    last_fill = color;
  }
  void StrokeRect(const render::Rect&, render::Color, float) override {}
  void FillRoundedRect(const render::Rect&, const render::CornerRadius&, render::Color) override {}
  void StrokeRoundedRect(const render::Rect&, const render::CornerRadius&, render::Color,
                         float) override {}
  void DrawTextRun(std::wstring_view, const render::Rect&, const render::TextStyle&,
                   render::Color, render::TextAlign, render::VerticalAlign) override {
    ++text_runs;
  }
  void DrawImage(render::ImageHandle, const render::Rect&, float) override { ++images; }
  void PushClip(const render::Rect&) override {}
  void PopClip() override {}
  void PushTranslation(render::Point) override {}
  void PopTranslation() override {}

  int loads = 0;
  int releases = 0;
  int measure_calls = 0;
  int fills = 0;
  int images = 0;
  int text_runs = 0;
  render::Color last_fill{};
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
    "dark": { "accent": "#60A5FA", "window": "#14161B" },
    "light": { "accent": "#2563EB", "window": "#F5F6F9" }
  },
  "common": { "properties": { "id": { "kind": "string" } } },
  "allows_children": ["screen"],
  "components": {
    "screen": { "properties": {
      "route_id": { "kind": "string", "required": true },
      "backdrop": { "kind": "string" }
    } },
    "text": { "properties": { "text": { "kind": "text", "required": true } } }
  }
})";

std::unique_ptr<ui::config::ResolvedUiDocument> Resolve(const char* screens) {
  json::Value core;
  DHEPZ_CHECK(json::Parse(W(kCore), &core).ok());
  std::vector<ui::config::Diagnostic> diagnostics;
  std::unique_ptr<ui::config::ResolvedUiDocument> document;
  const core::Status status =
      ui::config::ResolveDocument(core, {{L"embedded", W(screens)}}, &diagnostics, &document);
  return document;
}

}  // namespace

DHEPZ_TEST(Backdrop, ColorBackdropMakesTheFillTransparentAndPaintsTheToken) {
  const auto document = Resolve(R"({
    "components": [
      { "type": "screen", "route_id": "home", "backdrop": "accent", "children": [
        { "type": "text", "text": "front" } ] }
    ]
  })");
  const ui::layout::PaintPlan plan = ui::layout::MakePaintPlan(*document, L"home");
  DHEPZ_CHECK(plan.content_fill_transparent);

  RecordingBackend backend;
  ui::layout::LayoutEngine engine(&backend);
  ui::layout::PaintBackdrop(&backend, &engine, *document, L"home", {400.0f, 300.0f}, L"dark",
                            nullptr);
  DHEPZ_CHECK_EQ(backend.fills, 1);
  DHEPZ_CHECK_EQ(static_cast<int>(backend.last_fill.r), 0x60);
  DHEPZ_CHECK_EQ(static_cast<int>(backend.last_fill.g), 0xA5);
  DHEPZ_CHECK_EQ(static_cast<int>(backend.last_fill.b), 0xFA);
}

DHEPZ_TEST(Backdrop, NoBackdropKeepsTheOpaqueFill) {
  const auto document = Resolve(R"({
    "components": [
      { "type": "screen", "route_id": "plain", "children": [
        { "type": "text", "text": "x" } ] }
    ]
  })");
  const ui::layout::PaintPlan plan = ui::layout::MakePaintPlan(*document, L"plain");
  DHEPZ_CHECK_FALSE(plan.content_fill_transparent);
}

DHEPZ_TEST(Backdrop, ScreenBackdropPaintsFromCachedLayoutWithoutRemeasuring) {
  const auto document = Resolve(R"({
    "components": [
      { "type": "screen", "route_id": "bg", "children": [
        { "type": "text", "text": "behind" } ] },
      { "type": "screen", "route_id": "front", "backdrop": "screen:bg", "children": [
        { "type": "text", "text": "front" } ] }
    ]
  })");
  RecordingBackend backend;
  ui::layout::LayoutEngine engine(&backend);
  const render::Size size{400.0f, 300.0f};

  // Warm the backdrop route once.
  engine.LayoutRoute(*document, L"bg", size, nullptr);
  const int warmed = backend.measure_calls;

  // Front frames repaint the backdrop without any new measurement.
  ui::layout::PaintBackdrop(&backend, &engine, *document, L"front", size, L"dark", nullptr);
  DHEPZ_CHECK_EQ(backend.measure_calls, warmed);
  DHEPZ_CHECK(backend.text_runs >= 1);  // the backdrop text painted
}

DHEPZ_TEST(Backdrop, ImageBackdropLoadsDrawsAndReleases) {
  const auto document = Resolve(R"({
    "components": [
      { "type": "screen", "route_id": "art", "backdrop": "assets/wall.png", "children": [
        { "type": "text", "text": "x" } ] }
    ]
  })");
  RecordingBackend backend;
  ui::layout::LayoutEngine engine(&backend);
  ui::layout::PaintBackdrop(&backend, &engine, *document, L"art", {400.0f, 300.0f}, L"dark",
                            nullptr);
  DHEPZ_CHECK_EQ(backend.loads, 1);
  DHEPZ_CHECK_EQ(backend.images, 1);
  DHEPZ_CHECK_EQ(backend.releases, 1);
}

DHEPZ_TEST(Backdrop, MissingBackdropScreenIsAResolveError) {
  json::Value core;
  DHEPZ_CHECK(json::Parse(W(kCore), &core).ok());
  std::vector<ui::config::Diagnostic> diagnostics;
  std::unique_ptr<ui::config::ResolvedUiDocument> document;
  const core::Status status = ui::config::ResolveDocument(
      core,
      {{L"embedded",
        W(R"({ "components": [
             { "type": "screen", "route_id": "home", "backdrop": "screen:nope" }
           ] })")}},
      &diagnostics, &document);
  DHEPZ_CHECK(!status.ok());
}
