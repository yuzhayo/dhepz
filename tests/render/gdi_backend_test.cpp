#include "render/gdi_backend.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "framework/test_case.h"

namespace {

LRESULT CALLBACK TestWindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  // The contract from gdi_backend.h: the window procedure returns 1 for
  // WM_ERASEBKGND. Suppressing the erase is what removes the flash.
  if (message == WM_ERASEBKGND) {
    return render::GdiBackend::EraseBackgroundResult();
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

HWND CreateTestWindow(int width, int height) {
  WNDCLASSW window_class{};
  window_class.lpfnWndProc = TestWindowProc;
  window_class.hInstance = GetModuleHandleW(nullptr);
  window_class.lpszClassName = L"dhepz.gdi.test";
  RegisterClassW(&window_class);  // already-exists is fine
  return CreateWindowExW(0, window_class.lpszClassName, L"", WS_POPUP, 0, 0, width, height,
                         nullptr, nullptr, window_class.hInstance, nullptr);
}

// Pixel packing is (a<<24)|(r<<16)|(g<<8)|b — see gdi_backend.cpp.
constexpr std::uint32_t Packed(std::uint8_t a, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
  return (static_cast<std::uint32_t>(a) << 24) | (static_cast<std::uint32_t>(r) << 16) |
         (static_cast<std::uint32_t>(g) << 8) | b;
}

bool NearChannel(std::uint32_t actual, std::uint32_t expected, std::uint32_t tolerance) {
  const auto diff = [](std::uint32_t x, std::uint32_t y) {
    return x > y ? x - y : y - x;
  };
  for (int shift = 0; shift <= 16; shift += 8) {
    if (diff((actual >> shift) & 0xFFu, (expected >> shift) & 0xFFu) > tolerance) {
      return false;
    }
  }
  return true;
}

std::wstring TempPath(const wchar_t* name) {
  wchar_t buffer[MAX_PATH] = {};
  GetTempPathW(MAX_PATH, buffer);
  return std::wstring(buffer) + name;
}

// Writes a tiny uncompressed 24-bit BMP: two red pixels over two blue ones.
bool WriteTestBitmap(const std::wstring& path) {
  const int width = 2;
  const int height = 2;
  const std::size_t row_bytes = ((width * 24 + 31) / 32) * 4;
  const std::size_t pixel_bytes = row_bytes * height;

  std::vector<std::uint8_t> file(sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) +
                                 pixel_bytes);
  auto* file_header = reinterpret_cast<BITMAPFILEHEADER*>(file.data());
  file_header->bfType = 0x4D42;  // 'BM'
  file_header->bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
  file_header->bfSize = static_cast<DWORD>(file.size());
  auto* info = reinterpret_cast<BITMAPINFOHEADER*>(file.data() + sizeof(BITMAPFILEHEADER));
  info->biSize = sizeof(BITMAPINFOHEADER);
  info->biWidth = width;
  info->biHeight = height;  // bottom-up
  info->biPlanes = 1;
  info->biBitCount = 24;
  info->biCompression = BI_RGB;

  std::uint8_t* pixels = file.data() + file_header->bfOffBits;
  // Bottom row first (bottom-up): blue, blue. Then top row: red, red.
  // BGR byte order.
  pixels[0] = 255; pixels[1] = 0; pixels[2] = 0;      // blue
  pixels[3] = 255; pixels[4] = 0; pixels[5] = 0;      // blue
  pixels[row_bytes + 0] = 0; pixels[row_bytes + 1] = 0; pixels[row_bytes + 2] = 255;  // red
  pixels[row_bytes + 3] = 0; pixels[row_bytes + 4] = 0; pixels[row_bytes + 5] = 255;  // red

  HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  const bool ok = WriteFile(handle, file.data(), static_cast<DWORD>(file.size()), &written,
                            nullptr) && written == file.size();
  CloseHandle(handle);
  return ok;
}

}  // namespace

DHEPZ_TEST(GdiBackend, BufferTracksSizeAndDpi) {
  render::GdiBackend backend;
  backend.Resize({100.0f, 50.0f});
  DHEPZ_CHECK_EQ(backend.buffer_width(), 100);
  DHEPZ_CHECK_EQ(backend.buffer_height(), 50);
  DHEPZ_CHECK_EQ(backend.surface_size().width, 100.0f);

  // Doubling the DPI doubles the physical buffer, not the logical surface.
  backend.SetDpi(192.0f);
  DHEPZ_CHECK_EQ(backend.buffer_width(), 200);
  DHEPZ_CHECK_EQ(backend.buffer_height(), 100);
  DHEPZ_CHECK_EQ(backend.surface_size().width, 100.0f);
  DHEPZ_CHECK_EQ(backend.dpi(), 192.0f);
}

DHEPZ_TEST(GdiBackend, DrawsIntoTheBufferAndPresentsWithOneBitBlt) {
  HWND window = CreateTestWindow(100, 100);
  DHEPZ_CHECK(window != nullptr);
  // A hidden window's DC does not persist blits between GetDC sessions;
  // show it so the present is observable.
  ShowWindow(window, SW_SHOW);

  render::GdiBackend backend;
  backend.Resize({100.0f, 100.0f});
  backend.BeginFrame({0.0f, 0.0f, 100.0f, 100.0f});
  backend.FillRect({0.0f, 0.0f, 100.0f, 100.0f}, {255, 0, 0, 255});
  backend.EndFrame();

  DHEPZ_CHECK_EQ(static_cast<unsigned long long>(backend.PixelAt(50, 50)), static_cast<unsigned long long>(Packed(255, 255, 0, 0)));

  backend.Present(window);
  const HDC dc = GetDC(window);
  const COLORREF pixel = GetPixel(dc, 50, 50);
  ReleaseDC(window, dc);
  DHEPZ_CHECK_EQ(static_cast<unsigned long long>(pixel),
                 static_cast<unsigned long long>(RGB(255, 0, 0)));

  DestroyWindow(window);
}

DHEPZ_TEST(GdiBackend, AlphaBlendsOverTheDestination) {
  render::GdiBackend backend;
  backend.Resize({10.0f, 10.0f});
  backend.BeginFrame({0.0f, 0.0f, 10.0f, 10.0f});
  backend.FillRect({0.0f, 0.0f, 10.0f, 10.0f}, {0, 0, 255, 255});
  backend.FillRect({0.0f, 0.0f, 10.0f, 10.0f}, {255, 0, 0, 127});
  backend.EndFrame();

  // 127/255 red over opaque blue: red ~127, blue ~128, alpha stays opaque.
  const std::uint32_t pixel = backend.PixelAt(5, 5);
  DHEPZ_CHECK((NearChannel(pixel, Packed(255, 127, 0, 128), 2)));
}

DHEPZ_TEST(GdiBackend, RoundedCornersAreAntialiasedAndClipped) {
  render::GdiBackend backend;
  backend.Resize({40.0f, 40.0f});
  backend.BeginFrame({0.0f, 0.0f, 40.0f, 40.0f});
  backend.FillRoundedRect({0.0f, 0.0f, 40.0f, 40.0f}, render::CornerRadius::Uniform(10.0f),
                          {255, 255, 255, 255});
  backend.EndFrame();

  // Center is fully covered; the exact corner pixel is outside the curve.
  DHEPZ_CHECK_EQ(static_cast<unsigned long long>(backend.PixelAt(20, 20)), static_cast<unsigned long long>(Packed(255, 255, 255, 255)));
  DHEPZ_CHECK_EQ(static_cast<unsigned long long>(backend.PixelAt(0, 0)), static_cast<unsigned long long>(Packed(255, 0, 0, 0)));
  // Somewhere in the corner region there must be a blended (non-pure) pixel
  // — that is the anti-aliasing. Scan a grid: along the exact diagonal the
  // pixel centers can skip the half-coverage band entirely at some radii.
  bool found_blend = false;
  for (int y = 0; y < 12 && !found_blend; ++y) {
    for (int x = 0; x < 12 && !found_blend; ++x) {
      const std::uint32_t pixel = backend.PixelAt(x, y);
      const std::uint32_t gray = (pixel >> 8) & 0xFFu;
      found_blend = gray > 5 && gray < 250;
    }
  }
  DHEPZ_CHECK(found_blend);
}

DHEPZ_TEST(GdiBackend, MeasurementWorksOutsideAndIsRefusedInsidePaint) {
  render::GdiBackend backend;
  backend.Resize({200.0f, 50.0f});

  const render::TextStyle style;
  const render::Size small = backend.MeasureText(L"Hi", style, 0.0f);
  const render::Size wide = backend.MeasureText(L"Hello, world", style, 0.0f);
  DHEPZ_CHECK(small.width > 0.0f);
  DHEPZ_CHECK(small.height > 0.0f);
  DHEPZ_CHECK(wide.width > small.width);

  // The guard: layout measurement belongs before the frame. Inside it, the
  // measurement DC is refused and nothing is measured.
  backend.BeginFrame({0.0f, 0.0f, 200.0f, 50.0f});
  DHEPZ_CHECK(backend.in_paint_scope());
  const render::Size refused = backend.MeasureText(L"Hello", style, 0.0f);
  DHEPZ_CHECK_EQ(refused.width, 0.0f);
  DHEPZ_CHECK_EQ(refused.height, 0.0f);
  backend.EndFrame();
  DHEPZ_CHECK_FALSE(backend.in_paint_scope());

  // And it works again afterwards.
  DHEPZ_CHECK(backend.MeasureText(L"Hello", style, 0.0f).width > 0.0f);
}

DHEPZ_TEST(GdiBackend, ClipAndTranslationStacksShapeDrawing) {
  render::GdiBackend backend;
  backend.Resize({40.0f, 40.0f});
  backend.BeginFrame({0.0f, 0.0f, 40.0f, 40.0f});
  backend.FillRect({0.0f, 0.0f, 40.0f, 40.0f}, {0, 0, 255, 255});

  backend.PushClip({10.0f, 10.0f, 10.0f, 10.0f});
  backend.FillRect({0.0f, 0.0f, 40.0f, 40.0f}, {255, 0, 0, 255});
  backend.PopClip();

  DHEPZ_CHECK_EQ(static_cast<unsigned long long>(backend.PixelAt(5, 5)), static_cast<unsigned long long>(Packed(255, 0, 0, 255)));    // outside clip: blue
  DHEPZ_CHECK_EQ(static_cast<unsigned long long>(backend.PixelAt(15, 15)), static_cast<unsigned long long>(Packed(255, 255, 0, 0)));  // inside clip: red

  backend.PushTranslation({20.0f, 20.0f});
  backend.FillRect({0.0f, 0.0f, 5.0f, 5.0f}, {0, 255, 0, 255});
  backend.PopTranslation();
  backend.EndFrame();

  DHEPZ_CHECK_EQ(static_cast<unsigned long long>(backend.PixelAt(22, 22)), static_cast<unsigned long long>(Packed(255, 0, 255, 0)));  // translated
  DHEPZ_CHECK_EQ(static_cast<unsigned long long>(backend.PixelAt(2, 2)), static_cast<unsigned long long>(Packed(255, 0, 0, 255)));    // untouched
}

DHEPZ_TEST(GdiBackend, ImagesDecodeDrawAndRelease) {
  const std::wstring path = TempPath(L"dhepz_gdi_test.bmp");
  DHEPZ_CHECK(WriteTestBitmap(path));

  render::GdiBackend backend;
  backend.Resize({4.0f, 4.0f});

  const render::ImageHandle image = backend.LoadImageFile(path);
  DHEPZ_CHECK(image != render::ImageHandle::Invalid);

  backend.BeginFrame({0.0f, 0.0f, 4.0f, 4.0f});
  backend.DrawImage(image, {0.0f, 0.0f, 4.0f, 4.0f}, 1.0f);
  backend.EndFrame();

  // The 2x2 source scales to 4x4: top rows red, bottom rows blue.
  DHEPZ_CHECK_EQ(static_cast<unsigned long long>(backend.PixelAt(1, 1)), static_cast<unsigned long long>(Packed(255, 255, 0, 0)));
  DHEPZ_CHECK_EQ(static_cast<unsigned long long>(backend.PixelAt(1, 3)), static_cast<unsigned long long>(Packed(255, 0, 0, 255)));

  backend.ReleaseImage(image);
  DHEPZ_CHECK(backend.LoadImageFile(TempPath(L"dhepz_gdi_missing.bmp")) ==
              render::ImageHandle::Invalid);
  DeleteFileW(path.c_str());
}

DHEPZ_TEST(GdiBackend, EraseBackgroundIsSuppressed) {
  HWND window = CreateTestWindow(50, 50);
  DHEPZ_CHECK(window != nullptr);
  const LRESULT result = SendMessageW(window, WM_ERASEBKGND, 0, 0);
  DHEPZ_CHECK_EQ(static_cast<long long>(result), static_cast<long long>(1));
  DestroyWindow(window);
}

DHEPZ_TEST(GdiBackend, HundredCyclesLeaveGdiHandlesFlat) {
  // One warm-up cycle first: the first GDI use in a process can create a
  // one-time OS artifact, and that is not the leak this test hunts.
  {
    render::GdiBackend warmup;
    warmup.Resize({50.0f, 50.0f});
    const render::TextStyle style;
    warmup.MeasureText(L"warm", style, 0.0f);
    warmup.BeginFrame({0.0f, 0.0f, 50.0f, 50.0f});
    warmup.FillRect({0.0f, 0.0f, 50.0f, 50.0f}, {1, 2, 3, 255});
    warmup.EndFrame();
  }

  const HANDLE process = GetCurrentProcess();
  const DWORD gdi_before = GetGuiResources(process, GR_GDIOBJECTS);
  const DWORD user_before = GetGuiResources(process, GR_USEROBJECTS);

  for (int i = 0; i < 100; ++i) {
    render::GdiBackend backend;
    backend.Resize({200.0f, 200.0f});
    const render::TextStyle style;
    backend.MeasureText(L"warm the font cache", style, 0.0f);
    backend.BeginFrame({0.0f, 0.0f, 200.0f, 200.0f});
    backend.FillRect({0.0f, 0.0f, 200.0f, 200.0f}, {10, 20, 30, 255});
    backend.DrawTextRun(L"text", {0.0f, 0.0f, 100.0f, 20.0f}, style, {255, 255, 255, 255},
                     render::TextAlign::Left, render::VerticalAlign::Top);
    backend.EndFrame();
  }

  const DWORD gdi_after = GetGuiResources(process, GR_GDIOBJECTS);
  const DWORD user_after = GetGuiResources(process, GR_USEROBJECTS);
  DHEPZ_CHECK_EQ(static_cast<unsigned long long>(gdi_after),
                 static_cast<unsigned long long>(gdi_before));
  // USER objects are process-global bookkeeping and can shrink between
  // samples; growth is the leak this test hunts.
  const long long user_drift = static_cast<long long>(user_after) -
                               static_cast<long long>(user_before);
  DHEPZ_CHECK(user_drift <= 2);
}

#ifdef NDEBUG
DHEPZ_TEST(GdiBackend, FullFrameFitsTheResizeBudgetInRelease) {
  HWND window = CreateTestWindow(800, 600);
  DHEPZ_CHECK(window != nullptr);
  render::GdiBackend backend;
  backend.Resize({800.0f, 600.0f});

  LARGE_INTEGER frequency{};
  QueryPerformanceFrequency(&frequency);
  LARGE_INTEGER start{};
  QueryPerformanceCounter(&start);

  backend.BeginFrame({0.0f, 0.0f, 800.0f, 600.0f});
  backend.FillRect({0.0f, 0.0f, 800.0f, 600.0f}, {30, 30, 30, 255});
  backend.EndFrame();
  backend.Present(window);

  LARGE_INTEGER end{};
  QueryPerformanceCounter(&end);
  const double ms = static_cast<double>(end.QuadPart - start.QuadPart) * 1000.0 /
                    static_cast<double>(frequency.QuadPart);
  DHEPZ_CHECK(ms < 16.7);
  DestroyWindow(window);
}
#endif

DHEPZ_TEST(GdiBackend, InvalidationsCoalesceIntoOneUnion) {
  render::GdiBackend backend;
  backend.Resize({100.0f, 100.0f});
  render::Rect drain{};
  DHEPZ_CHECK(backend.TakeInvalidation(drain));  // fresh-buffer invalidation
  DHEPZ_CHECK_FALSE(backend.HasInvalidation());

  // Ten separate invalidations in one message-loop iteration...
  for (int i = 0; i < 10; ++i) {
    backend.Invalidate({static_cast<float>(i * 5), 10.0f, 4.0f, 4.0f});
  }
  DHEPZ_CHECK(backend.HasInvalidation());

  // ...are taken as exactly one region: their bounding union.
  render::Rect dirty{};
  DHEPZ_CHECK(backend.TakeInvalidation(dirty));
  DHEPZ_CHECK_EQ(dirty.x, 0.0f);
  DHEPZ_CHECK_EQ(dirty.y, 10.0f);
  DHEPZ_CHECK_EQ(dirty.right(), 49.0f);
  DHEPZ_CHECK_EQ(dirty.bottom(), 14.0f);

  // Take clears: nothing pending until the next invalidation.
  DHEPZ_CHECK_FALSE(backend.HasInvalidation());
  DHEPZ_CHECK_FALSE(backend.TakeInvalidation(dirty));
}

DHEPZ_TEST(GdiBackend, InvalidationIsClippedToTheSurface) {
  render::GdiBackend backend;
  backend.Resize({50.0f, 50.0f});
  render::Rect drain{};
  DHEPZ_CHECK(backend.TakeInvalidation(drain));  // fresh-buffer invalidation

  backend.Invalidate({-20.0f, -20.0f, 40.0f, 40.0f});
  render::Rect dirty{};
  DHEPZ_CHECK(backend.TakeInvalidation(dirty));
  DHEPZ_CHECK_EQ(dirty.x, 0.0f);
  DHEPZ_CHECK_EQ(dirty.y, 0.0f);
  DHEPZ_CHECK_EQ(dirty.right(), 20.0f);
  DHEPZ_CHECK_EQ(dirty.bottom(), 20.0f);

  // Fully outside the surface: nothing pending.
  backend.Invalidate({60.0f, 60.0f, 10.0f, 10.0f});
  DHEPZ_CHECK_FALSE(backend.HasInvalidation());
}

DHEPZ_TEST(GdiBackend, AResizedBufferInvalidatesEverything) {
  render::GdiBackend backend;
  backend.Resize({40.0f, 40.0f});
  render::Rect dirty{};
  DHEPZ_CHECK(backend.TakeInvalidation(dirty));
  DHEPZ_CHECK_EQ(dirty.width, 40.0f);
  DHEPZ_CHECK_EQ(dirty.height, 40.0f);

  // Same size again: nothing new pending.
  backend.Resize({40.0f, 40.0f});
  DHEPZ_CHECK_FALSE(backend.HasInvalidation());

  // A real resize marks the whole surface dirty again.
  backend.Resize({60.0f, 30.0f});
  DHEPZ_CHECK(backend.TakeInvalidation(dirty));
  DHEPZ_CHECK_EQ(dirty.width, 60.0f);
  DHEPZ_CHECK_EQ(dirty.height, 30.0f);
}

DHEPZ_TEST(GdiBackend, PaintTouchesOnlyTheDirtyRegion) {
  render::GdiBackend backend;
  backend.Resize({40.0f, 40.0f});

  // First frame paints the base (the fresh buffer is fully invalidated).
  render::Rect dirty{};
  DHEPZ_CHECK(backend.TakeInvalidation(dirty));
  backend.BeginFrame(dirty);
  backend.FillRect({0.0f, 0.0f, 40.0f, 40.0f}, {0, 0, 255, 255});
  backend.EndFrame();

  // A caret-sized invalidation: the next frame is clipped to it, so a
  // full-surface draw only changes those pixels.
  backend.Invalidate({10.0f, 10.0f, 2.0f, 20.0f});
  DHEPZ_CHECK(backend.TakeInvalidation(dirty));
  DHEPZ_CHECK_EQ(dirty.width, 2.0f);
  backend.BeginFrame(dirty);
  backend.FillRect({0.0f, 0.0f, 40.0f, 40.0f}, {255, 0, 0, 255});
  backend.EndFrame();

  DHEPZ_CHECK_EQ(static_cast<unsigned long long>(backend.PixelAt(11, 15)),
                 static_cast<unsigned long long>(Packed(255, 255, 0, 0)));  // dirty: red
  DHEPZ_CHECK_EQ(static_cast<unsigned long long>(backend.PixelAt(5, 15)),
                 static_cast<unsigned long long>(Packed(255, 0, 0, 255)));  // outside: blue
  DHEPZ_CHECK_EQ(static_cast<unsigned long long>(backend.PixelAt(30, 30)),
                 static_cast<unsigned long long>(Packed(255, 0, 0, 255)));  // outside: blue
}

DHEPZ_TEST(GdiBackend, InvalidationWhileIdleOnlyAccumulates) {
  render::GdiBackend backend;
  backend.Resize({20.0f, 20.0f});
  render::Rect dirty{};
  DHEPZ_CHECK(backend.TakeInvalidation(dirty));  // fresh-buffer invalidation

  // With no frame run, invalidating changes nothing in the buffer and arms
  // nothing: it merely accumulates until someone paints.
  backend.Invalidate({0.0f, 0.0f, 5.0f, 5.0f});
  DHEPZ_CHECK_EQ(static_cast<unsigned long long>(backend.PixelAt(2, 2)),
                 static_cast<unsigned long long>(Packed(255, 0, 0, 0)));
  DHEPZ_CHECK(backend.HasInvalidation());
}
