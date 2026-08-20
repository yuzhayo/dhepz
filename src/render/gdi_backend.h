// The GDI implementation of RenderBackend (#16), and the per-window back
// buffer everything draws into.
//
// One instance serves one window surface. The shell creates a backend per
// window, sizes it on WM_SIZE, and presents from its message handler.
//
// How the drawing is split, and why:
//
//   - Fills, strokes, rounded shapes and images run through the software
//     compositor directly into the DIB pixels. GDI fills into a 32-bpp DIB
//     cannot do alpha, and rounded corners need coverage anti-aliasing to
//     look right — G2 includes appearance, not just speed.
//   - Text goes through GDI (DrawTextW/ExtTextOut) because shaping and
//     hinting are not worth rewriting. The buffer's alpha channel is forced
//     opaque over the drawn bounds afterwards, keeping the compositor's
//     invariant that the buffer is opaque everywhere.
//   - Image decoding covers uncompressed BMP (24/32-bit) for now; PNG and
//     icon formats join with the first component that needs them. Deliberate:
//     no new system library for a capability nothing consumes yet.
//
// The paint-scope guard: fonts and the measurement DC are REFUSED while a
// frame is open. Resource creation belongs before BeginFrame (layout warms
// the caches); creating inside a frame is the bug this guard exists to
// catch, and it asserts loudly in debug builds.
//
// WM_ERASEBKGND: the window procedure must return EraseBackgroundResult()
// (that is, 1) — suppressing the erase is what removes the background flash,
// because this backend never paints through the window DC directly.
//
// This header stays free of windows.h; the implementation owns every GDI
// object and releases each one on every path, including errors.
#pragma once

#include <memory>

#include "render/render_backend.h"

namespace render {

class GdiBackend final : public RenderBackend {
 public:
  GdiBackend();
  ~GdiBackend() override;

  GdiBackend(const GdiBackend&) = delete;
  GdiBackend& operator=(const GdiBackend&) = delete;

  // RenderBackend — surface management.
  void Resize(Size logical_size) override;
  void SetDpi(float dpi) override;
  float dpi() const override;
  Size surface_size() const override;

  // RenderBackend — frame scope.
  void BeginFrame(const Rect& dirty) override;
  void EndFrame() override;
  void Present(void* window_handle) override;

  // RenderBackend — resources.
  ImageHandle LoadImageFile(std::wstring_view path) override;
  void ReleaseImage(ImageHandle image) override;

  // RenderBackend — measurement. Returns {0, 0} when called inside a paint
  // scope: measurement belongs to layout, which runs before the frame.
  Size MeasureText(std::wstring_view text, const TextStyle& style, float max_width) override;

  // RenderBackend — drawing.
  void FillRect(const Rect& rect, Color color) override;
  void StrokeRect(const Rect& rect, Color color, float stroke_width) override;
  void FillRoundedRect(const Rect& rect, const CornerRadius& radius, Color color) override;
  void StrokeRoundedRect(const Rect& rect, const CornerRadius& radius, Color color,
                         float stroke_width) override;
  void DrawTextRun(std::wstring_view text, const Rect& bounds, const TextStyle& style, Color color,
                TextAlign horizontal, VerticalAlign vertical) override;
  void DrawImage(ImageHandle image, const Rect& dest, float opacity) override;

  // RenderBackend — clip and translation stacks.
  void PushClip(const Rect& rect) override;
  void PopClip() override;
  void PushTranslation(Point offset) override;
  void PopTranslation() override;

  // The value a window procedure returns for WM_ERASEBKGND. See the header
  // comment: 1 suppresses the erase and removes the background flash.
  static long long EraseBackgroundResult() { return 1; }

  // Verification hooks — used by the tests to see the buffer directly.
  bool in_paint_scope() const;
  int buffer_width() const;   // physical pixels
  int buffer_height() const;  // physical pixels
  std::uint32_t PixelAt(int x, int y) const;  // 0xAABBGGRR, or 0 out of range

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace render
