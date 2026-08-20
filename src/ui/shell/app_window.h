// The one real window: custom frame, and the show/hide lifecycle the
// responsiveness budget depends on. Routing, tabs and components arrive in
// Phase 2 — this is the container.
//
// The lifecycle rules, because they are the point of this file:
//
//   - Hide (or minimise, or close): the back buffer and every window-scoped
//     resource are released. The buffer is the one large allocation and it
//     scales with window size; keeping it while hidden is the drift G1
//     forbids.
//   - Show/restore: a full frame is rendered offscreen BEFORE ShowWindow,
//     so the window never appears empty and then fills in.
//   - Close hides. The process stays tray-resident; it does not exit. Exit
//     belongs to the tray menu.
//
// Presentation goes through UpdateLayeredWindow: the buffer carries
// per-pixel alpha, which buys rounded corners and a soft shadow with no
// second window and no DWM trickery.
//
// This header stays free of windows.h.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "render/gdi_backend.h"

namespace render {
class GdiResourceCache;
}

namespace platform {
class SignalFanout;
}

namespace shell {

class AppWindow final {
 public:
  // With a shared cache, fonts survive this window's close (the plan's
  // explicit choice); with none, the backend keeps a private cache.
  explicit AppWindow(render::GdiResourceCache* shared_cache = nullptr);
  ~AppWindow();

  AppWindow(const AppWindow&) = delete;
  AppWindow& operator=(const AppWindow&) = delete;

  // Content size in logical pixels; the window itself is larger by the
  // shadow margin on each side.
  bool Create(void* instance, float content_width = 960.0f, float content_height = 640.0f);

  // Renders a full frame offscreen, then shows. Idempotent while visible.
  void Show();
  // Releases the buffer and window-scoped resources; the window object
  // survives hidden.
  void Hide();
  // Resident semantics: Close() is Hide(). The process stays up.
  void Close();
  // Real teardown.
  void Destroy();

  bool alive() const { return hwnd_ != nullptr; }
  bool visible() const;
  bool maximized() const { return maximized_; }
  void* hwnd() const { return hwnd_; }
  render::GdiBackend* backend() { return &backend_; }

  // Verification hooks: the lifecycle steps the message handler takes,
  // callable directly so tests do not need a message pump.
  void OnResized(int width_px, int height_px);
  void OnDpiChanged(float dpi, int suggested_width_px, int suggested_height_px);
  // Physical client coordinates -> non-client hit zone (HTCAPTION,
  // HTLEFT..., HTCLIENT, HTTRANSPARENT for the shadow margin).
  int HitTest(int x_px, int y_px) const;

  // OS broadcast signals (theme, display, colours, settings) arrive in
  // bursts; the fan-out coalesces each burst into one drain. The handler
  // receives the drained bitmask; phases that care (themes) set one here.
  void set_signal_handler(std::function<void(std::uint32_t)> handler);
  std::uint32_t last_os_signals() const { return last_os_signals_; }

 private:
  static long long __stdcall WindowProc(void* window, unsigned int message,
                                        unsigned long long wparam, long long lparam);
  // `window` is the live HWND from the proc — the hwnd_ member is not set
  // yet during creation messages.
  long long HandleMessage(void* window, unsigned int message, unsigned long long wparam,
                          long long lparam);

  void RenderFullFrame();
  void PaintContent();
  void SetMaximized(bool maximize);
  int Px(float logical) const;
  float Logical(int px) const;
  // Button index (0 min, 1 max/restore, 2 close) at physical client coords,
  // or -1.
  int ButtonAt(int x_px, int y_px) const;

  void* instance_ = nullptr;
  void* hwnd_ = nullptr;  // HWND
  render::GdiBackend backend_;
  std::unique_ptr<platform::SignalFanout> signals_;
  std::function<void(std::uint32_t)> signal_handler_;
  std::uint32_t last_os_signals_ = 0;
  unsigned int drain_message_ = 0;
  float dpi_ = 96.0f;
  bool maximized_ = false;
  int restored_width_px_ = 0;
  int restored_height_px_ = 0;
  int hover_button_ = -1;
  std::wstring title_ = L"dhepz";
};

}  // namespace shell
