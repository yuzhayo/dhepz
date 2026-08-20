// Proof that the render seam holds: this file includes render_backend.h
// with no backend implementation anywhere in the build, implements the
// interface with a recording stub, and exercises the contract. If a Win32
// type ever leaks into the header, this translation unit stops compiling.

#include "render/render_backend.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "framework/test_case.h"

namespace {

class StubBackend final : public render::RenderBackend {
 public:
  std::vector<std::string> calls;
  float dpi_value = 96.0f;
  render::Size size = {800.0f, 600.0f};
  std::uint64_t next_image = 1;
  int live_images = 0;

  void Resize(render::Size logical_size) override {
    size = logical_size;
    calls.push_back("resize");
  }
  void SetDpi(float dpi) override {
    dpi_value = dpi;
    calls.push_back("set_dpi");
  }
  float dpi() const override { return dpi_value; }
  render::Size surface_size() const override { return size; }

  void BeginFrame(const render::Rect&) override { calls.push_back("begin"); }
  void EndFrame() override { calls.push_back("end"); }
  void Present(void*) override { calls.push_back("present"); }

  void Invalidate(const render::Rect&) override { calls.push_back("invalidate"); }
  void InvalidateAll() override { calls.push_back("invalidate_all"); }
  bool HasInvalidation() const override { return false; }
  bool TakeInvalidation(render::Rect&) override { return false; }

  render::ImageHandle LoadImageFile(std::wstring_view) override {
    ++live_images;
    calls.push_back("load_image");
    return static_cast<render::ImageHandle>(next_image++);
  }
  void ReleaseImage(render::ImageHandle) override {
    --live_images;
    calls.push_back("release_image");
  }

  render::Size MeasureText(std::wstring_view text, const render::TextStyle&, float) override {
    calls.push_back("measure");
    // Deterministic fake: one advance per code unit. A real backend measures
    // glyphs; the seam test only needs the call to work outside a frame.
    return {static_cast<float>(text.size()) * 7.0f, 14.0f};
  }

  void FillRect(const render::Rect&, render::Color) override { calls.push_back("fill_rect"); }
  void StrokeRect(const render::Rect&, render::Color, float) override {
    calls.push_back("stroke_rect");
  }
  void FillRoundedRect(const render::Rect&, const render::CornerRadius&, render::Color) override {
    calls.push_back("fill_rounded");
  }
  void StrokeRoundedRect(const render::Rect&, const render::CornerRadius&, render::Color,
                         float) override {
    calls.push_back("stroke_rounded");
  }
  void DrawTextRun(std::wstring_view, const render::Rect&, const render::TextStyle&, render::Color,
                render::TextAlign, render::VerticalAlign) override {
    calls.push_back("draw_text");
  }
  void DrawImage(render::ImageHandle, const render::Rect&, float) override {
    calls.push_back("draw_image");
  }

  void PushClip(const render::Rect&) override { calls.push_back("push_clip"); }
  void PopClip() override { calls.push_back("pop_clip"); }
  void PushTranslation(render::Point) override { calls.push_back("push_translation"); }
  void PopTranslation() override { calls.push_back("pop_translation"); }
};

bool Contains(const std::vector<std::string>& calls, const std::string& name) {
  for (const auto& call : calls) {
    if (call == name) {
      return true;
    }
  }
  return false;
}

// MSVC constant-folds trivial accessors on known values into the check
// macro's `if`, which is C4127 under /WX. A noinline boundary keeps the
// assertion a runtime check, which is what it is meant to be.
__declspec(noinline) bool RectEmptyAtRuntime(const render::Rect& rect) { return rect.empty(); }

}  // namespace

DHEPZ_TEST(RenderBackend, StubImplementsTheWholeSurfaceWithoutWin32) {
  StubBackend backend;
  render::RenderBackend& surface = backend;

  surface.Resize({1024.0f, 768.0f});
  surface.SetDpi(144.0f);
  DHEPZ_CHECK_EQ(surface.dpi(), 144.0f);
  DHEPZ_CHECK_EQ(surface.surface_size().width, 1024.0f);

  // Measurement works with no frame open — the layout-before-paint contract.
  const render::TextStyle style;
  const render::Size measured = surface.MeasureText(L"hello", style, 0.0f);
  DHEPZ_CHECK_EQ(measured.width, 5.0f * 7.0f);

  const render::ImageHandle image = surface.LoadImageFile(L"icon.png");
  DHEPZ_CHECK(image != render::ImageHandle::Invalid);

  int window = 0;  // opaque to the interface
  surface.BeginFrame({0.0f, 0.0f, 1024.0f, 768.0f});
  surface.FillRect({0.0f, 0.0f, 100.0f, 100.0f}, {255, 0, 0, 255});
  surface.StrokeRect({0.0f, 0.0f, 100.0f, 100.0f}, {0, 0, 0, 255}, 1.0f);
  surface.FillRoundedRect({0.0f, 0.0f, 100.0f, 100.0f}, render::CornerRadius::Uniform(8.0f),
                          {0, 120, 215, 255});
  surface.StrokeRoundedRect({0.0f, 0.0f, 100.0f, 100.0f}, render::CornerRadius::Uniform(8.0f),
                            {0, 0, 0, 255}, 1.0f);
  surface.PushClip({10.0f, 10.0f, 80.0f, 80.0f});
  surface.PushTranslation({5.0f, 5.0f});
  surface.DrawTextRun(L"tab", {0.0f, 0.0f, 80.0f, 20.0f}, style, {255, 255, 255, 255},
                   render::TextAlign::Center, render::VerticalAlign::Middle);
  surface.DrawImage(image, {0.0f, 0.0f, 16.0f, 16.0f}, 1.0f);
  surface.PopTranslation();
  surface.PopClip();
  surface.EndFrame();
  surface.Present(&window);

  surface.ReleaseImage(image);
  DHEPZ_CHECK_EQ(backend.live_images, 0);

  // The frame order is the contract a compositor implements against.
  DHEPZ_CHECK(Contains(backend.calls, "measure"));
  DHEPZ_CHECK(Contains(backend.calls, "fill_rounded"));
  const auto& calls = backend.calls;
  const auto begin = std::find(calls.begin(), calls.end(), "begin");
  const auto end = std::find(calls.begin(), calls.end(), "end");
  const auto fill = std::find(calls.begin(), calls.end(), "fill_rect");
  const auto present = std::find(calls.begin(), calls.end(), "present");
  DHEPZ_CHECK(begin != calls.end());
  DHEPZ_CHECK(end != calls.end());
  DHEPZ_CHECK(fill > begin && fill < end);
  DHEPZ_CHECK(present > end);
}

DHEPZ_TEST(RenderBackend, ValueTypesBehave) {
  const render::Rect rect{10.0f, 20.0f, 30.0f, 40.0f};
  DHEPZ_CHECK_EQ(rect.right(), 40.0f);
  DHEPZ_CHECK_EQ(rect.bottom(), 60.0f);
  // Extra parens: braces alone do not shield commas from the preprocessor,
  // and an unparenthesised braced-init-list splits a macro argument.
  DHEPZ_CHECK((rect.contains({10.0f, 20.0f})));
  DHEPZ_CHECK_FALSE((rect.contains({40.0f, 60.0f})));  // right/bottom edges are exclusive
  DHEPZ_CHECK(RectEmptyAtRuntime({0.0f, 0.0f, 0.0f, 5.0f}));

  const render::CornerRadius uniform = render::CornerRadius::Uniform(6.0f);
  DHEPZ_CHECK_EQ(uniform.top_left, 6.0f);
  DHEPZ_CHECK_EQ(uniform.bottom_right, 6.0f);

  // Zero is never a valid handle. Multiplying an opaque address by zero
  // keeps the right-hand side from being a compile-time constant (C4127
  // under /WX) while still evaluating to exactly 0 at runtime.
  int opaque = 0;
  const auto sentinel =
      static_cast<render::ImageHandle>(reinterpret_cast<std::uintptr_t>(&opaque) * 0u);
  DHEPZ_CHECK(render::ImageHandle::Invalid == sentinel);
}
