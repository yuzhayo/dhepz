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
class SettleTimer;
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
  bool Create(void* instance, float content_width = 430.0f, float content_height = 430.0f);

  // Renders a full frame offscreen, then shows. Idempotent while visible.
  void Show();
  // Releases the buffer and window-scoped resources; the window object
  // survives hidden.
  void Hide();
  // Resident semantics: Close() is Hide(). The process stays up.
  void Close();
  // Re-renders a visible window after a parent-owned state patch.
  void Repaint();
  // Real teardown.
  void Destroy();

  bool alive() const { return hwnd_ != nullptr; }
  bool visible() const;
  // Observed from the OS (Aero snap / future toggle maximize it; the shell
  // only adapts its margins).
  bool maximized() const;
  void* hwnd() const { return hwnd_; }
  render::GdiBackend* backend() { return &backend_; }

  // Always-on-top pin: the left-most caption button. The state is
  // session-only — persistence belongs to the settings module.
  void TogglePin();
  bool pinned() const { return pinned_; }

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

  // Fires once after a burst of activity (resize, DPI change) settles.
  // The underlying timer exists only while activity is ongoing — never at
  // idle. This is the hook settle-time measurement (ETW) attaches to.
  void set_settle_handler(std::function<void()> handler);

  // Opens the settings screen (Phase 5 wires this). While no handler is
  // registered the caption's settings button does not render at all — no
  // dead UI. Registering a handler adds the button.
  void set_settings_handler(std::function<void()> handler);

  // Content integration (#71): the shell paints its frame and delegates the
  // content area to a presenter. The painter runs inside the frame scope,
  // after the chrome. Key/click handlers get first chance for clicks inside
  // the content area and for WM_KEYDOWN; returning true means "handled,
  // repaint". Unset hooks cost nothing.
  void set_content_painter(std::function<void(render::GdiBackend&, const render::Rect&)> painter);
  // Runs before BeginFrame with the content rect: measurement (layout) must
  // happen outside the paint scope — the backend refuses font creation
  // inside a frame. Unset costs nothing.
  void set_content_layout(std::function<void(const render::Rect&)> layout);
  void set_content_key_handler(std::function<bool(int virtual_key)> handler);
  void set_content_text_handler(std::function<bool(wchar_t character)> handler);
  void set_content_click_handler(std::function<bool(float x_logical, float y_logical)> handler);
  // Hover and press feedback for content components; return true to repaint.
  void set_content_move_handler(std::function<bool(float x_logical, float y_logical)> handler);
  void set_content_down_handler(std::function<bool(float x_logical, float y_logical)> handler);
  // The caption band is HTCAPTION for dragging; content that lives in that
  // band (the tab strip) claims its points through this hook as HTCLIENT,
  // otherwise clicks become caption drags and hover never arrives.
  void set_content_hittest_handler(std::function<bool(float x_logical, float y_logical)> handler);

  // Narrow lifecycle seams used by the production composition owner.
  void set_visibility_handler(std::function<void(bool visible)> handler);
  void set_frame_presented_handler(std::function<void()> handler);
  void set_message_handler(
      std::function<bool(unsigned int message, unsigned long long wparam,
                         long long lparam)> handler);

 private:
  static long long __stdcall WindowProc(void* window, unsigned int message,
                                        unsigned long long wparam, long long lparam);
  // `window` is the live HWND from the proc — the hwnd_ member is not set
  // yet during creation messages.
  long long HandleMessage(void* window, unsigned int message, unsigned long long wparam,
                          long long lparam);

  void RenderFullFrame();
  void PaintContent();
  int Px(float logical) const;
  float Logical(int px) const;
  // Caption buttons left-to-right: pin, settings (only while a settings
  // handler is registered), close. Returns the left-to-right index or -1.
  int ButtonAt(int x_px, int y_px) const;
  int ButtonCount() const;

  void* instance_ = nullptr;
  void* hwnd_ = nullptr;  // HWND
  render::GdiBackend backend_;
  std::unique_ptr<platform::SignalFanout> signals_;
  std::unique_ptr<platform::SettleTimer> settle_timer_;
  std::function<void(std::uint32_t)> signal_handler_;
  std::function<void()> settle_handler_;
  std::function<void()> settings_handler_;
  std::function<void(render::GdiBackend&, const render::Rect&)> content_painter_;
  std::function<void(const render::Rect&)> content_layout_;
  std::function<bool(int)> content_key_handler_;
  std::function<bool(wchar_t)> content_text_handler_;
  std::function<bool(float, float)> content_click_handler_;
  std::function<bool(float, float)> content_move_handler_;
  std::function<bool(float, float)> content_down_handler_;
  std::function<bool(float, float)> content_hittest_handler_;
  std::function<void(bool)> visibility_handler_;
  std::function<void()> frame_presented_handler_;
  std::function<bool(unsigned int, unsigned long long, long long)> message_handler_;
  std::uint32_t last_os_signals_ = 0;
  unsigned int drain_message_ = 0;
  float dpi_ = 96.0f;
  bool pinned_ = false;
  int hover_button_ = -1;
  std::wstring title_ = L"dhepz";
};

}  // namespace shell
