// The rendering seam: the full drawing surface the UI is allowed to use,
// declared before any implementation exists behind it.
//
// HYRUM'S LAW NOTE — read before changing anything in this header.
// Fourteen components, the window frame, and the theme layer will be written
// against this surface. Every signature, enum value, and documented behaviour
// here becomes a contract the moment the second consumer exists. A change
// after that point touches all of them. If this interface must change, the
// reason goes in the PR, and the Phase 1 gate (#23) is where such a decision
// is still cheap. Direct2D is the planned escape hatch — it is only taken if
// GDI misses the responsiveness targets, and this header is what keeps that
// swap a contained change instead of a rewrite.
//
// THE CONTRACT
//
// Ownership and lifetime
//   - The backend owns every resource it hands out. Callers hold handles,
//     never the resources. Image handles must be released with ReleaseImage;
//     everything else (fonts, brushes, pens, buffers) is backend-internal
//     and bounded by the cache layer.
//   - Fonts are not handles at all: callers describe text with a TextStyle
//     value, and the backend resolves it to a cached font internally. This
//     keeps the hot path allocation-free of resource bookkeeping and lets
//     the cache key fonts by (style, dpi, epoch).
//
// Paint scope
//   - Drawing calls (Fill*, Stroke*, Draw*, Push*/Pop*) are valid only
//     between BeginFrame and EndFrame. Outside that scope they are errors.
//   - MeasureText, LoadImageFile, ReleaseImage, Resize, and SetDpi are valid at
//     any time. Measurement in particular MUST work without a live paint
//     scope: layout runs before paint, often several times per frame.
//
// Frames and presentation
//   - The shell calls Resize whenever the window size or DPI changes, then
//     BeginFrame(dirty) to open a frame clipped to the dirty region, draws,
//     EndFrame, and Present(window) to put the buffer on screen. Nothing is
//     presented that was not drawn this frame into the back buffer.
//
// DPI
//   - SetDpi changes the scale all metrics and drawing are expressed in.
//     A DPI change implies a resource-epoch bump: cached fonts and tiles
//     from the old DPI are invalid and are rebuilt lazily. Callers do not
//     scale coordinates themselves; they draw in logical pixels at the
//     current DPI.
//
// This header is standard library only. If a Win32 type ever appears here,
// the seam no longer exists — the check is a grep, and it is part of the
// acceptance criteria.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace render {

// --- Value types -----------------------------------------------------------

struct Color {
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
  std::uint8_t a = 255;
};

struct Point {
  float x = 0.0f;
  float y = 0.0f;
};

struct Size {
  float width = 0.0f;
  float height = 0.0f;
};

struct Rect {
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;

  float right() const { return x + width; }
  float bottom() const { return y + height; }
  bool empty() const { return width <= 0.0f || height <= 0.0f; }
  bool contains(Point p) const {
    return p.x >= x && p.x < right() && p.y >= y && p.y < bottom();
  }
};

// Per-corner radii so a card can round two corners and not the others.
struct CornerRadius {
  float top_left = 0.0f;
  float top_right = 0.0f;
  float bottom_right = 0.0f;
  float bottom_left = 0.0f;

  static CornerRadius Uniform(float radius) {
    return CornerRadius{radius, radius, radius, radius};
  }
};

enum class FontWeight { Normal, Medium, Semibold, Bold };

enum class TextAlign {
  Left,
  Center,
  Right,
};

enum class VerticalAlign {
  Top,
  Middle,
  Bottom,
};

// A font description, not a font. The backend resolves it to a cached
// resource keyed by (family, size, weight, italic, dpi, epoch).
struct TextStyle {
  std::wstring family = L"Segoe UI";
  float size_px = 14.0f;
  FontWeight weight = FontWeight::Normal;
  bool italic = false;
};

struct EditableTextVisual {
  std::size_t selection_start = 0;
  std::size_t selection_end = 0;
  std::size_t caret = 0;
  Color selection_color{};
  Color caret_color{};
};

// Opaque, backend-owned. Zero is never a valid handle.
enum class ImageHandle : std::uint64_t { Invalid = 0 };

// --- The interface ----------------------------------------------------------

class RenderBackend {
 public:
  virtual ~RenderBackend() = default;

  // Surface management. Valid at any time.
  virtual void Resize(Size logical_size) = 0;
  virtual void SetDpi(float dpi) = 0;
  virtual float dpi() const = 0;
  virtual Size surface_size() const = 0;

  // Frame scope. Drawing is only valid between BeginFrame and EndFrame.
  virtual void BeginFrame(const Rect& dirty) = 0;
  virtual void EndFrame() = 0;
  // Puts the back buffer on the window. The window handle is opaque here;
  // the shell knows what it really is.
  virtual void Present(void* window_handle) = 0;

  // Invalidation. A component marks what changed; the regions accumulate,
  // coalescing into their union. TakeInvalidation atomically takes and
  // clears the pending region — the shell then opens exactly one frame
  // clipped to it, so ten invalidations in one message-loop iteration cost
  // one paint. Invalidating while no window is visible merely accumulates:
  // nothing paints and nothing is armed (G1). Regions are clipped to the
  // surface.
  virtual void Invalidate(const Rect& region) = 0;
  virtual void InvalidateAll() = 0;
  virtual bool HasInvalidation() const = 0;
  virtual bool TakeInvalidation(Rect& out) = 0;

  // Resources. Valid at any time; the backend owns what it returns.
  virtual ImageHandle LoadImageFile(std::wstring_view path) = 0;
  virtual void ReleaseImage(ImageHandle image) = 0;

  // Measurement. Valid OUTSIDE a paint scope — layout depends on it.
  // max_width 0 means unbounded. Returns the size the text would occupy.
  virtual Size MeasureText(std::wstring_view text, const TextStyle& style,
                           float max_width) = 0;

  // Drawing. Valid only inside a paint scope.
  virtual void FillRect(const Rect& rect, Color color) = 0;
  virtual void StrokeRect(const Rect& rect, Color color, float stroke_width) = 0;
  virtual void FillRoundedRect(const Rect& rect, const CornerRadius& radius, Color color) = 0;
  virtual void StrokeRoundedRect(const Rect& rect, const CornerRadius& radius, Color color,
                                 float stroke_width) = 0;
  virtual void DrawTextRun(std::wstring_view text, const Rect& bounds, const TextStyle& style,
                         Color color, TextAlign horizontal, VerticalAlign vertical) = 0;
  // Editable text needs glyph-exact selection and caret placement. The
  // backend already owns the font metrics used to draw the run, so those
  // pixels stay here instead of being estimated by an input component.
  virtual void DrawEditableTextRun(std::wstring_view text, const Rect& bounds,
                                   const TextStyle& style, Color color,
                                   const EditableTextVisual& visual) {
    (void)visual;
    DrawTextRun(text, bounds, style, color, TextAlign::Left, VerticalAlign::Middle);
  }
  virtual void DrawImage(ImageHandle image, const Rect& dest, float opacity) = 0;

  // Clip and translation stacks. Every Push must be balanced by a Pop
  // before EndFrame; a frame ends with an empty stack.
  virtual void PushClip(const Rect& rect) = 0;
  virtual void PopClip() = 0;
  virtual void PushTranslation(Point offset) = 0;
  virtual void PopTranslation() = 0;
};

}  // namespace render
