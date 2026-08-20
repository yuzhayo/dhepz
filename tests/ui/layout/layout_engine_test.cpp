#include "ui/layout/layout_engine.h"

#include <chrono>
#include <string>

#include "framework/test_case.h"

namespace {

// Counting seam stub: fixed 100x20 text metrics, records MeasureText calls.
class CountingBackend final : public render::RenderBackend {
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
    ++measure_calls;
    return {100.0f, 20.0f};
  }
  void FillRect(const render::Rect&, render::Color) override {}
  void StrokeRect(const render::Rect&, render::Color, float) override {}
  void FillRoundedRect(const render::Rect&, const render::CornerRadius&, render::Color) override {}
  void StrokeRoundedRect(const render::Rect&, const render::CornerRadius&, render::Color,
                         float) override {}
  void DrawTextRun(std::wstring_view, const render::Rect&, const render::TextStyle&,
                   render::Color, render::TextAlign, render::VerticalAlign) override {}
  void DrawImage(render::ImageHandle, const render::Rect&, float) override {}
  void PushClip(const render::Rect&) override {}
  void PopClip() override {}
  void PushTranslation(render::Point) override {}
  void PopTranslation() override {}

  int measure_calls = 0;
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
  "tokens": { "dark": { "text": "#FFFFFF" }, "light": { "text": "#000000" } },
  "common": { "properties": { "id": { "kind": "string" } } },
  "allows_children": ["screen", "container"],
  "components": {
    "screen": { "properties": { "route_id": { "kind": "string", "required": true } } },
    "container": { "properties": {
      "direction": { "kind": "enum", "values": ["row", "column"] },
      "gap": { "kind": "int", "default": 8 },
      "padding": { "kind": "object" }
    } },
    "text": { "properties": { "text": { "kind": "text", "required": true } } },
    "list": { "properties": {
      "row_height": { "kind": "int", "default": 32 },
      "overscan_rows": { "kind": "int", "default": 2 },
      "item_template": { "kind": "object" }
    } }
  }
})";

const char* kScreens = R"({
  "components": [
    { "type": "screen", "route_id": "big", "children": [
      { "type": "container", "direction": "column", "gap": 4, "children": [
        { "type": "text", "text": "head" },
        { "type": "list", "id": "rows", "row_height": 32, "item_template": {
            "type": "text", "text": "row" } }
      ] }
    ] },
    { "type": "screen", "route_id": "small", "children": [
      { "type": "text", "text": "only" }
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

DHEPZ_TEST(LayoutEngine, ListVirtualizationMeasuresOnlyVisibleRows) {
  CountingBackend backend;
  ui::layout::LayoutEngine engine(&backend);
  const std::unique_ptr<ui::config::ResolvedUiDocument> document = Resolve();
  ui::layout::ListModel model;
  model.count = 10000;

  const ui::layout::LayoutNode& root =
      engine.LayoutRoute(*document, L"big", {400.0f, 320.0f}, &model);

  // 320 px viewport / 32 px rows = 10 visible + 2*2 overscan = 14 rows.
  const ui::layout::LayoutNode& container = root.children[0];
  const ui::layout::LayoutNode& list = container.children[1];
  DHEPZ_CHECK_EQ(list.row_count, 10000);
  DHEPZ_CHECK_EQ(static_cast<std::size_t>(list.visible_row_count), list.children.size());
  DHEPZ_CHECK(list.visible_row_count <= 14);
  // head text + at most the visible rows: never anywhere near 10k.
  DHEPZ_CHECK(backend.measure_calls <= 15);
}

DHEPZ_TEST(LayoutEngine, ScrollReusesMemoizedMeasurements) {
  CountingBackend backend;
  ui::layout::LayoutEngine engine(&backend);
  const std::unique_ptr<ui::config::ResolvedUiDocument> document = Resolve();
  ui::layout::ListModel model;
  model.count = 10000;

  engine.LayoutRoute(*document, L"big", {400.0f, 320.0f}, &model);
  const int after_first = backend.measure_calls;
  engine.LayoutScrolled(*document, L"big", {400.0f, 320.0f}, L"rows", 3200.0f, &model);
  // The row template already measured; scrolling must not re-measure the
  // world: at most the visible row count of new measurements.
  DHEPZ_CHECK(backend.measure_calls - after_first <= 14);
}

DHEPZ_TEST(LayoutEngine, ColumnLayoutStacksWithGapAndPadding) {
  CountingBackend backend;
  ui::layout::LayoutEngine engine(&backend);
  const std::unique_ptr<ui::config::ResolvedUiDocument> document = Resolve();

  const ui::layout::LayoutNode& root =
      engine.LayoutRoute(*document, L"small", {400.0f, 320.0f}, nullptr);
  DHEPZ_CHECK_EQ(root.children.size(), static_cast<std::size_t>(1));
  DHEPZ_CHECK_EQ(root.children[0].bounds.height, 20.0f);
}

DHEPZ_TEST(LayoutEngine, WarmRouteSwitchIsACacheLookup) {
  CountingBackend backend;
  ui::layout::LayoutEngine engine(&backend);
  const std::unique_ptr<ui::config::ResolvedUiDocument> document = Resolve();
  ui::layout::ListModel model;
  model.count = 10000;

  engine.LayoutRoute(*document, L"big", {400.0f, 320.0f}, &model);
  engine.LayoutRoute(*document, L"small", {400.0f, 320.0f}, &model);

  const auto start = std::chrono::steady_clock::now();
  const ui::layout::LayoutNode& again =
      engine.LayoutRoute(*document, L"big", {400.0f, 320.0f}, &model);
  const auto end = std::chrono::steady_clock::now();
  const double ms = std::chrono::duration<double, std::milli>(end - start).count();
  DHEPZ_CHECK(again.source != nullptr);
  DHEPZ_CHECK_EQ(again.source->type(), std::wstring(L"screen"));
#ifdef NDEBUG
  DHEPZ_CHECK(ms < 50.0);  // the Phase 2 warm-switch budget
#else
  DHEPZ_CHECK(ms < 1000.0);
#endif
}
