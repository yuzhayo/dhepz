#include "render/gdi_backend.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace render {
namespace {

// --- Compositor -------------------------------------------------------------
//
// Pixel packing is (a<<24)|(r<<16)|(g<<8)|b. Stored little-endian that is
// the byte order [B,G,R,A] a 32-bpp DIB expects, so GDI and the compositor
// agree on the buffer without any conversion.

constexpr std::uint32_t Channel(std::uint32_t pixel, int shift) noexcept {
  return (pixel >> shift) & 0xFFu;
}

// Exact /255 without a divide (the old build's trick, kept because it is
// both faster and rounded correctly).
constexpr std::uint32_t DivideBy255(std::uint32_t value) noexcept {
  return (value + 128u + ((value + 128u) >> 8u)) >> 8u;
}

std::uint32_t Pack(const Color& color) noexcept {
  return (static_cast<std::uint32_t>(color.a) << 24) |
         (static_cast<std::uint32_t>(color.r) << 16) |
         (static_cast<std::uint32_t>(color.g) << 8) | static_cast<std::uint32_t>(color.b);
}

std::uint32_t Premultiply(const Color& color) noexcept {
  const std::uint32_t alpha = color.a;
  const std::uint32_t red = DivideBy255(static_cast<std::uint32_t>(color.r) * alpha);
  const std::uint32_t green = DivideBy255(static_cast<std::uint32_t>(color.g) * alpha);
  const std::uint32_t blue = DivideBy255(static_cast<std::uint32_t>(color.b) * alpha);
  return (alpha << 24) | (red << 16) | (green << 8) | blue;
}

std::uint32_t SourceOverPremultiplied(std::uint32_t destination, std::uint32_t source) noexcept {
  const std::uint32_t inverse_alpha = 255u - Channel(source, 24);
  const std::uint32_t blue = Channel(source, 0) + DivideBy255(Channel(destination, 0) * inverse_alpha);
  const std::uint32_t green = Channel(source, 8) + DivideBy255(Channel(destination, 8) * inverse_alpha);
  const std::uint32_t red = Channel(source, 16) + DivideBy255(Channel(destination, 16) * inverse_alpha);
  const std::uint32_t alpha = Channel(source, 24) + DivideBy255(Channel(destination, 24) * inverse_alpha);
  return (std::min(255u, alpha) << 24) | (std::min(255u, red) << 16) |
         (std::min(255u, green) << 8) | std::min(255u, blue);
}

struct ImageData {
  int width = 0;
  int height = 0;
  std::vector<std::uint32_t> pixels;  // premultiplied, same packing as the buffer
};

struct FontKey {
  std::wstring family;
  int height_px = 0;
  int weight = FW_NORMAL;
  bool italic = false;

  bool operator<(const FontKey& other) const noexcept {
    if (family != other.family) return family < other.family;
    if (height_px != other.height_px) return height_px < other.height_px;
    if (weight != other.weight) return weight < other.weight;
    return italic < other.italic;
  }
};

int ToGdiWeight(FontWeight weight) noexcept {
  switch (weight) {
    case FontWeight::Normal: return FW_NORMAL;
    case FontWeight::Medium: return FW_MEDIUM;
    case FontWeight::Semibold: return FW_SEMIBOLD;
    case FontWeight::Bold: return FW_BOLD;
  }
  return FW_NORMAL;
}

// Decodes an uncompressed 24/32-bit BI_RGB BMP. PNG and icon formats arrive
// with the first component that needs them — see the header.
bool DecodeBmp(const std::uint8_t* data, std::size_t size, ImageData* out) {
  if (size < sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)) return false;
  BITMAPFILEHEADER file_header{};
  std::memcpy(&file_header, data, sizeof(file_header));
  BITMAPINFOHEADER info{};
  std::memcpy(&info, data + sizeof(BITMAPFILEHEADER), sizeof(info));
  if (info.biCompression != BI_RGB || (info.biBitCount != 24 && info.biBitCount != 32)) {
    return false;
  }
  const int width = info.biWidth;
  const int height_abs = std::abs(info.biHeight);
  const bool top_down = info.biHeight < 0;
  if (width <= 0 || height_abs <= 0 || file_header.bfOffBits >= size) return false;

  const std::size_t row_bytes = ((static_cast<std::size_t>(width) * info.biBitCount + 31) / 32) * 4;
  const std::size_t needed = file_header.bfOffBits + row_bytes * height_abs;
  if (needed > size) return false;

  out->width = width;
  out->height = height_abs;
  out->pixels.resize(static_cast<std::size_t>(width) * height_abs);
  for (int y = 0; y < height_abs; ++y) {
    const int source_y = top_down ? y : (height_abs - 1 - y);
    const std::uint8_t* row = data + file_header.bfOffBits + row_bytes * source_y;
    std::uint32_t* dest = out->pixels.data() + static_cast<std::size_t>(y) * width;
    for (int x = 0; x < width; ++x) {
      const std::uint8_t blue = row[x * info.biBitCount / 8 + 0];
      const std::uint8_t green = row[x * info.biBitCount / 8 + 1];
      const std::uint8_t red = row[x * info.biBitCount / 8 + 2];
      const std::uint8_t alpha = info.biBitCount == 32 ? row[x * 4 + 3] : 255;
      Color color{red, green, blue, alpha};
      dest[x] = Premultiply(color);
    }
  }
  return true;
}

}  // namespace

// --- Implementation -----------------------------------------------------------

struct GdiBackend::Impl {
  float dpi = 96.0f;
  Size logical_size = {0.0f, 0.0f};

  HDC buffer_dc = nullptr;
  HBITMAP bitmap = nullptr;
  HGDIOBJ previous_bitmap = nullptr;
  std::uint32_t* pixels = nullptr;
  int buffer_width = 0;
  int buffer_height = 0;

  bool in_paint_scope = false;
  Rect dirty{};

  std::vector<Rect> clips;
  std::vector<Point> translations;

  HDC measurement_dc = nullptr;
  std::map<FontKey, HFONT> fonts;
  std::map<std::uint64_t, ImageData> images;
  std::uint64_t next_image = 1;

  float scale() const noexcept { return dpi / 96.0f; }

  int Physical(float value) const noexcept {
    return static_cast<int>(std::lround(value * scale()));
  }
  float Logical(int value) const noexcept { return value / scale(); }

  Point Translation() const noexcept {
    Point total{0.0f, 0.0f};
    for (const Point& offset : translations) {
      total.x += offset.x;
      total.y += offset.y;
    }
    return total;
  }

  // The effective clip in physical buffer coordinates: the surface, the
  // frame's dirty region, and every pushed clip, all offset by the current
  // translation. Returns an empty rect when nothing is drawable.
  RECT EffectiveClip() const noexcept {
    RECT clip{0, 0, buffer_width, buffer_height};
    if (in_paint_scope) {
      // LONG and int are distinct types to the template, so spell the type.
      clip.left = std::max<LONG>(clip.left, Physical(dirty.x));
      clip.top = std::max<LONG>(clip.top, Physical(dirty.y));
      clip.right = std::min<LONG>(clip.right, Physical(dirty.right()));
      clip.bottom = std::min<LONG>(clip.bottom, Physical(dirty.bottom()));
    }
    const Point translation = Translation();
    for (const Rect& pushed : clips) {
      clip.left = std::max<LONG>(clip.left, Physical(pushed.x + translation.x));
      clip.top = std::max<LONG>(clip.top, Physical(pushed.y + translation.y));
      clip.right = std::min<LONG>(clip.right, Physical(pushed.right() + translation.x));
      clip.bottom = std::min<LONG>(clip.bottom, Physical(pushed.bottom() + translation.y));
    }
    if (clip.right <= clip.left || clip.bottom <= clip.top) {
      return RECT{0, 0, 0, 0};
    }
    return clip;
  }

  RECT PhysicalRect(const Rect& rect) const noexcept {
    const Point translation = Translation();
    return RECT{Physical(rect.x + translation.x), Physical(rect.y + translation.y),
                Physical(rect.right() + translation.x), Physical(rect.bottom() + translation.y)};
  }

  bool EnsureBuffer(int width, int height) noexcept {
    if (width <= 0 || height <= 0) return false;
    if (buffer_dc != nullptr && buffer_width == width && buffer_height == height) return true;

    HDC candidate_dc = CreateCompatibleDC(nullptr);
    if (candidate_dc == nullptr) return false;

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;  // top-down rows
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* candidate_pixels = nullptr;
    HBITMAP candidate_bitmap =
        CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &candidate_pixels, nullptr, 0);
    if (candidate_bitmap == nullptr || candidate_pixels == nullptr) {
      if (candidate_bitmap != nullptr) DeleteObject(candidate_bitmap);
      DeleteDC(candidate_dc);
      return false;
    }
    const HGDIOBJ candidate_previous = SelectObject(candidate_dc, candidate_bitmap);
    if (candidate_previous == nullptr || candidate_previous == HGDI_ERROR) {
      DeleteObject(candidate_bitmap);
      DeleteDC(candidate_dc);
      return false;
    }

    ReleaseBuffer();
    buffer_dc = candidate_dc;
    bitmap = candidate_bitmap;
    previous_bitmap = candidate_previous;
    pixels = static_cast<std::uint32_t*>(candidate_pixels);
    buffer_width = width;
    buffer_height = height;
    // The compositor's invariant: the buffer is opaque everywhere.
    std::fill_n(pixels, static_cast<std::size_t>(width) * height, 0xFF000000u);
    return true;
  }

  void ReleaseBuffer() noexcept {
    if (buffer_dc != nullptr && previous_bitmap != nullptr) {
      SelectObject(buffer_dc, previous_bitmap);
    }
    if (bitmap != nullptr) DeleteObject(bitmap);
    if (buffer_dc != nullptr) DeleteDC(buffer_dc);
    buffer_dc = nullptr;
    bitmap = nullptr;
    previous_bitmap = nullptr;
    pixels = nullptr;
    buffer_width = 0;
    buffer_height = 0;
  }

  // The measurement DC is lazy and refused inside a paint scope — see the
  // header. The refusal IS the guard: debug output names the violation, the
  // caller gets nothing, and the test suite fails loudly if the contract
  // breaks. (A hard assert here would abort the very test that proves the
  // refusal works.)
  HDC MeasurementDc() noexcept {
    if (in_paint_scope) {
      OutputDebugStringW(L"dhepz: measurement DC refused inside a paint scope\n");
      return nullptr;
    }
    if (measurement_dc == nullptr) {
      measurement_dc = CreateCompatibleDC(nullptr);
    }
    return measurement_dc;
  }

  HFONT FontFor(const TextStyle& style) noexcept {
    FontKey key{style.family, Physical(style.size_px), ToGdiWeight(style.weight), style.italic};
    if (const auto found = fonts.find(key); found != fonts.end()) {
      return found->second;
    }
    if (in_paint_scope) {
      // Fonts are warmed by layout (MeasureText) before the frame. Creating
      // one mid-frame is the bug the guard exists to catch.
      OutputDebugStringW(L"dhepz: font creation refused inside a paint scope\n");
      return nullptr;
    }
    const HFONT font = CreateFontW(-key.height_px, 0, 0, 0, key.weight, key.italic ? TRUE : FALSE,
                                   FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                   CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                   DEFAULT_PITCH | FF_DONTCARE, key.family.c_str());
    if (font != nullptr) {
      fonts.emplace(std::move(key), font);
    }
    return font;
  }

  // Restores the buffer's opaque-alpha invariant over a region after a GDI
  // draw, which writes undefined alpha into a 32-bpp DIB.
  void ForceOpaqueAlpha(const RECT& region) noexcept {
    if (pixels == nullptr) return;
    const RECT clipped{std::max(0L, region.left), std::max(0L, region.top),
                       std::min(static_cast<LONG>(buffer_width), region.right),
                       std::min(static_cast<LONG>(buffer_height), region.bottom)};
    for (LONG y = clipped.top; y < clipped.bottom; ++y) {
      std::uint32_t* row = pixels + static_cast<std::size_t>(y) * buffer_width;
      for (LONG x = clipped.left; x < clipped.right; ++x) {
        row[x] |= 0xFF000000u;
      }
    }
  }

  void CompositeSolid(const RECT& region, const Color& color) noexcept {
    if (pixels == nullptr || color.a == 0) return;
    const RECT clip = EffectiveClip();
    const RECT target{std::max(clip.left, region.left), std::max(clip.top, region.top),
                      std::min(clip.right, region.right), std::min(clip.bottom, region.bottom)};
    if (target.right <= target.left || target.bottom <= target.top) return;
    const std::uint32_t source = Premultiply(color);
    for (LONG y = target.top; y < target.bottom; ++y) {
      std::uint32_t* row = pixels + static_cast<std::size_t>(y) * buffer_width;
      for (LONG x = target.left; x < target.right; ++x) {
        row[x] = SourceOverPremultiplied(row[x], source);
      }
    }
  }

  // Coverage-anti-aliased rounded rect, ported from the old compositor and
  // extended to per-corner radii by picking the owning quadrant's radius.
  void CompositeRounded(const Rect& rect, const CornerRadius& radius, const Color& fill,
                        const Color& border, float border_width) noexcept {
    if (pixels == nullptr || (fill.a == 0 && border.a == 0)) return;
    const RECT clip = EffectiveClip();
    const RECT region = PhysicalRect(rect);
    const RECT target{std::max(clip.left, region.left), std::max(clip.top, region.top),
                      std::min(clip.right, region.right), std::min(clip.bottom, region.bottom)};
    if (target.right <= target.left || target.bottom <= target.top) return;

    const double width = static_cast<double>(region.right - region.left);
    const double height = static_cast<double>(region.bottom - region.top);
    const double limit = std::min(width, height) / 2.0;
    const double radii[4] = {
        std::clamp(static_cast<double>(Physical(radius.top_left)), 0.0, limit),
        std::clamp(static_cast<double>(Physical(radius.top_right)), 0.0, limit),
        std::clamp(static_cast<double>(Physical(radius.bottom_right)), 0.0, limit),
        std::clamp(static_cast<double>(Physical(radius.bottom_left)), 0.0, limit),
    };
    const double inset = std::clamp(static_cast<double>(Physical(border_width)), 0.0, limit);
    const double center_x = (region.left + region.right) / 2.0;
    const double center_y = (region.top + region.bottom) / 2.0;

    const auto coverage = [](double x, double y, double half_width, double half_height,
                             double shape_radius) {
      const double qx = std::abs(x) - (half_width - shape_radius);
      const double qy = std::abs(y) - (half_height - shape_radius);
      const double outside = std::hypot(std::max(qx, 0.0), std::max(qy, 0.0));
      const double inside = std::min(std::max(qx, qy), 0.0);
      return std::clamp(0.5 - (outside + inside - shape_radius), 0.0, 1.0);
    };

    for (LONG y = target.top; y < target.bottom; ++y) {
      std::uint32_t* row = pixels + static_cast<std::size_t>(y) * buffer_width;
      for (LONG x = target.left; x < target.right; ++x) {
        const double local_x = x + 0.5 - center_x;
        const double local_y = y + 0.5 - center_y;
        const int quadrant = (local_x >= 0.0 ? 1 : 0) + (local_y >= 0.0 ? 2 : 0);
        const double outer_radius = radii[quadrant];
        const double outer =
            coverage(local_x, local_y, width / 2.0, height / 2.0, outer_radius);
        if (outer <= 0.0) continue;

        double inner = 0.0;
        if (inset > 0.0) {
          const double inner_radius = std::max(0.0, outer_radius - inset);
          inner = coverage(local_x, local_y, width / 2.0 - inset, height / 2.0 - inset,
                           inner_radius);
          const double border_coverage = outer * (1.0 - inner);
          if (border_coverage > 0.0) {
            Color edge = border;
            edge.a = static_cast<std::uint8_t>(edge.a * border_coverage + 0.5);
            row[x] = SourceOverPremultiplied(row[x], Premultiply(edge));
          }
        }
        const double fill_coverage = inset > 0.0 ? inner : outer;
        if (fill_coverage > 0.0) {
          Color inner_fill = fill;
          inner_fill.a = static_cast<std::uint8_t>(inner_fill.a * fill_coverage + 0.5);
          row[x] = SourceOverPremultiplied(row[x], Premultiply(inner_fill));
        }
      }
    }
  }

  void ReleaseAll() noexcept {
    ReleaseBuffer();
    if (measurement_dc != nullptr) DeleteDC(measurement_dc);
    measurement_dc = nullptr;
    for (const auto& entry : fonts) {
      DeleteObject(entry.second);
    }
    fonts.clear();
    images.clear();
  }
};

GdiBackend::GdiBackend() : impl_(std::make_unique<Impl>()) {}
GdiBackend::~GdiBackend() { impl_->ReleaseAll(); }

void GdiBackend::Resize(Size logical_size) {
  impl_->logical_size = logical_size;
  impl_->EnsureBuffer(impl_->Physical(logical_size.width),
                      impl_->Physical(logical_size.height));
}

void GdiBackend::SetDpi(float dpi) {
  if (dpi <= 0.0f) return;
  impl_->dpi = dpi;
  // Fonts are keyed by physical height; a DPI change orphans them. The
  // epoch/cache machinery of #19 will manage this at scale.
  for (const auto& entry : impl_->fonts) {
    DeleteObject(entry.second);
  }
  impl_->fonts.clear();
  if (impl_->logical_size.width > 0.0f) {
    Resize(impl_->logical_size);
  }
}

float GdiBackend::dpi() const { return impl_->dpi; }
Size GdiBackend::surface_size() const { return impl_->logical_size; }

void GdiBackend::BeginFrame(const Rect& dirty) {
  if (impl_->pixels == nullptr) return;
  impl_->in_paint_scope = true;
  impl_->dirty = dirty;
  impl_->clips.clear();
  impl_->translations.clear();
}

void GdiBackend::EndFrame() {
  impl_->in_paint_scope = false;
  impl_->clips.clear();
  impl_->translations.clear();
}

void GdiBackend::Present(void* window_handle) {
  if (impl_->pixels == nullptr || window_handle == nullptr) return;
  const HWND window = static_cast<HWND>(window_handle);
  const HDC target = GetDC(window);
  if (target == nullptr) return;
  BitBlt(target, 0, 0, impl_->buffer_width, impl_->buffer_height, impl_->buffer_dc, 0, 0,
         SRCCOPY);
  ReleaseDC(window, target);
}

ImageHandle GdiBackend::LoadImageFile(std::wstring_view path) {
  ImageData image;
  HANDLE file = CreateFileW(std::wstring(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return ImageHandle::Invalid;

  std::vector<std::uint8_t> bytes;
  const DWORD size = GetFileSize(file, nullptr);
  if (size != INVALID_FILE_SIZE && size > 0 && size < 64u * 1024u * 1024u) {
    bytes.resize(size);
    DWORD read = 0;
    if (!ReadFile(file, bytes.data(), size, &read, nullptr) || read != size) {
      bytes.clear();
    }
  }
  CloseHandle(file);
  if (bytes.empty() || !DecodeBmp(bytes.data(), bytes.size(), &image)) {
    return ImageHandle::Invalid;
  }

  const std::uint64_t id = impl_->next_image++;
  impl_->images.emplace(id, std::move(image));
  return static_cast<ImageHandle>(id);
}

void GdiBackend::ReleaseImage(ImageHandle image) {
  impl_->images.erase(static_cast<std::uint64_t>(image));
}

Size GdiBackend::MeasureText(std::wstring_view text, const TextStyle& style, float max_width) {
  if (text.empty()) return {0.0f, 0.0f};
  HDC dc = impl_->MeasurementDc();
  HFONT font = impl_->FontFor(style);
  if (dc == nullptr || font == nullptr) return {0.0f, 0.0f};

  const HGDIOBJ previous = SelectObject(dc, font);
  RECT measured{0, 0, max_width > 0.0f ? std::max(1, impl_->Physical(max_width)) : 32767, 32767};
  const UINT flags = DT_CALCRECT | (max_width > 0.0f ? DT_WORDBREAK : 0);
  DrawTextW(dc, text.data(), static_cast<int>(text.size()), &measured, flags);
  if (previous != nullptr && previous != HGDI_ERROR) SelectObject(dc, previous);
  return {impl_->Logical(measured.right - measured.left),
          impl_->Logical(measured.bottom - measured.top)};
}

void GdiBackend::FillRect(const Rect& rect, Color color) {
  if (!impl_->in_paint_scope) return;
  impl_->CompositeSolid(impl_->PhysicalRect(rect), color);
}

void GdiBackend::StrokeRect(const Rect& rect, Color color, float stroke_width) {
  if (!impl_->in_paint_scope) return;
  const RECT region = impl_->PhysicalRect(rect);
  const int thickness = std::max(1, impl_->Physical(stroke_width));
  impl_->CompositeSolid(RECT{region.left, region.top, region.right, region.top + thickness},
                        color);
  impl_->CompositeSolid(RECT{region.left, region.bottom - thickness, region.right, region.bottom},
                        color);
  impl_->CompositeSolid(RECT{region.left, region.top, region.left + thickness, region.bottom},
                        color);
  impl_->CompositeSolid(RECT{region.right - thickness, region.top, region.right, region.bottom},
                        color);
}

void GdiBackend::FillRoundedRect(const Rect& rect, const CornerRadius& radius, Color color) {
  if (!impl_->in_paint_scope) return;
  impl_->CompositeRounded(rect, radius, color, Color{0, 0, 0, 0}, 0.0f);
}

void GdiBackend::StrokeRoundedRect(const Rect& rect, const CornerRadius& radius, Color color,
                                   float stroke_width) {
  if (!impl_->in_paint_scope) return;
  impl_->CompositeRounded(rect, radius, Color{0, 0, 0, 0}, color, stroke_width);
}

void GdiBackend::DrawTextRun(std::wstring_view text, const Rect& bounds, const TextStyle& style,
                          Color color, TextAlign horizontal, VerticalAlign vertical) {
  if (!impl_->in_paint_scope || text.empty() || impl_->buffer_dc == nullptr) return;
  HFONT font = impl_->FontFor(style);
  if (font == nullptr) return;  // uncached inside a frame: layout forgot to warm it

  const RECT clip = impl_->EffectiveClip();
  const RECT region = impl_->PhysicalRect(bounds);
  RECT visible{std::max(clip.left, region.left), std::max(clip.top, region.top),
               std::min(clip.right, region.right), std::min(clip.bottom, region.bottom)};
  if (visible.right <= visible.left || visible.bottom <= visible.top) return;

  // Single-line placement: measure, then position. Reflow at width is a
  // MeasureText concern for layout; painting places one run.
  RECT measured{0, 0, 32767, 32767};
  const HGDIOBJ previous = SelectObject(impl_->buffer_dc, font);
  DrawTextW(impl_->buffer_dc, text.data(), static_cast<int>(text.size()), &measured,
            DT_CALCRECT);

  int x = region.left;
  const int text_width = measured.right - measured.left;
  if (horizontal == TextAlign::Center) {
    x += ((region.right - region.left) - text_width) / 2;
  } else if (horizontal == TextAlign::Right) {
    x = region.right - text_width;
  }
  int y = region.top;
  const int text_height = measured.bottom - measured.top;
  if (vertical == VerticalAlign::Middle) {
    y += ((region.bottom - region.top) - text_height) / 2;
  } else if (vertical == VerticalAlign::Bottom) {
    y = region.bottom - text_height;
  }

  SetBkMode(impl_->buffer_dc, TRANSPARENT);
  SetTextColor(impl_->buffer_dc,
               RGB(color.r, color.g, color.b));  // GDI consumes COLORREF, packing handled above
  ExtTextOutW(impl_->buffer_dc, x, y, ETO_CLIPPED, &visible, text.data(),
              static_cast<int>(text.size()), nullptr);
  if (previous != nullptr && previous != HGDI_ERROR) SelectObject(impl_->buffer_dc, previous);
  impl_->ForceOpaqueAlpha(visible);
}

void GdiBackend::DrawImage(ImageHandle image, const Rect& dest, float opacity) {
  if (!impl_->in_paint_scope || impl_->pixels == nullptr || opacity <= 0.0f) return;
  const auto found = impl_->images.find(static_cast<std::uint64_t>(image));
  if (found == impl_->images.end()) return;
  const ImageData& source = found->second;

  const RECT clip = impl_->EffectiveClip();
  const RECT region = impl_->PhysicalRect(dest);
  const RECT target{std::max(clip.left, region.left), std::max(clip.top, region.top),
                    std::min(clip.right, region.right), std::min(clip.bottom, region.bottom)};
  if (target.right <= target.left || target.bottom <= target.top) return;

  const int dest_width = region.right - region.left;
  const int dest_height = region.bottom - region.top;
  if (dest_width <= 0 || dest_height <= 0) return;
  const float clamped_opacity = std::min(1.0f, opacity);

  for (LONG y = target.top; y < target.bottom; ++y) {
    const int source_y = static_cast<int>(static_cast<long long>(y - region.top) *
                                          source.height / dest_height);
    std::uint32_t* row = impl_->pixels + static_cast<std::size_t>(y) * impl_->buffer_width;
    for (LONG x = target.left; x < target.right; ++x) {
      const int source_x = static_cast<int>(static_cast<long long>(x - region.left) *
                                            source.width / dest_width);
      const std::uint32_t pixel =
          source.pixels[static_cast<std::size_t>(source_y) * source.width + source_x];
      const std::uint32_t alpha = Channel(pixel, 24);
      if (alpha == 0) continue;

      // The source is premultiplied: scaling every channel (alpha included)
      // by the opacity gives the effective source, then plain source-over
      // against the (opaque) destination.
      const std::uint32_t opacity255 = static_cast<std::uint32_t>(clamped_opacity * 255.0f);
      const std::uint32_t src_alpha = DivideBy255(alpha * opacity255);
      const std::uint32_t inverse = 255u - src_alpha;
      const std::uint32_t dst = row[x];
      const std::uint32_t b = DivideBy255(Channel(pixel, 0) * opacity255) +
                              DivideBy255(Channel(dst, 0) * inverse);
      const std::uint32_t g = DivideBy255(Channel(pixel, 8) * opacity255) +
                              DivideBy255(Channel(dst, 8) * inverse);
      const std::uint32_t r = DivideBy255(Channel(pixel, 16) * opacity255) +
                              DivideBy255(Channel(dst, 16) * inverse);
      const std::uint32_t a = src_alpha + DivideBy255(Channel(dst, 24) * inverse);
      row[x] = (std::min(255u, a) << 24) | (std::min(255u, r) << 16) |
               (std::min(255u, g) << 8) | std::min(255u, b);
    }
  }
}

void GdiBackend::PushClip(const Rect& rect) { impl_->clips.push_back(rect); }
void GdiBackend::PopClip() {
  if (!impl_->clips.empty()) impl_->clips.pop_back();
}
void GdiBackend::PushTranslation(Point offset) { impl_->translations.push_back(offset); }
void GdiBackend::PopTranslation() {
  if (!impl_->translations.empty()) impl_->translations.pop_back();
}

bool GdiBackend::in_paint_scope() const { return impl_->in_paint_scope; }
int GdiBackend::buffer_width() const { return impl_->buffer_width; }
int GdiBackend::buffer_height() const { return impl_->buffer_height; }
std::uint32_t GdiBackend::PixelAt(int x, int y) const {
  if (impl_->pixels == nullptr || x < 0 || y < 0 || x >= impl_->buffer_width ||
      y >= impl_->buffer_height) {
    return 0;
  }
  return impl_->pixels[static_cast<std::size_t>(y) * impl_->buffer_width + x];
}

}  // namespace render
