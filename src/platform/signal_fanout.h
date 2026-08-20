// Coalesced fan-out for OS broadcast signals: theme change, DPI change,
// display change, system colours, settings.
//
// The OS delivers these in bursts, and naively forwarding each one to every
// window turns one system event into an N×M storm of work. Instead:
//
//   - Signals accumulate into a bitmask. However many arrive between
//     drains, exactly ONE message is posted to the window.
//   - The drain takes the mask and clears it with std::exchange — a signal
//     arriving mid-drain re-posts and is not lost.
//   - If the post fails, the drain happens synchronously. Dropping a signal
//     silently is worse than doing the work inline (the old build did this
//     deliberately).
//   - With no window attached, signals are merely recorded and applied when
//     a window attaches — zero cost while nothing is open, and no timer is
//     armed anywhere in this mechanism.
//
// Single-thread note: raises are expected on the UI thread (broadcasts
// arrive through the window proc). The mask is atomic anyway, so a raise
// from a future worker thread is safe; the drain itself stays on the UI
// thread.
//
// This header stays free of windows.h.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>

namespace platform {

enum class OsSignal : std::uint32_t {
  Theme = 1u << 0,
  Dpi = 1u << 1,
  Display = 1u << 2,
  SystemColors = 1u << 3,
  Settings = 1u << 4,
};

class SignalFanout final {
 public:
  using Handler = std::function<void(std::uint32_t signals)>;

  // `window` may be null: signals are recorded until Attach() gives the
  // fan-out somewhere to drain to. `drain_message` is a
  // RegisterWindowMessage-style id the window proc routes to DrainMessage().
  SignalFanout(void* window, unsigned int drain_message, Handler handler);

  SignalFanout(const SignalFanout&) = delete;
  SignalFanout& operator=(const SignalFanout&) = delete;

  // Accumulates one signal. Posts at most one message per burst; drains
  // synchronously if the post fails.
  void Raise(OsSignal signal);

  // Called by the window proc for the drain message: takes the pending mask
  // (std::exchange — take-and-clear, so nothing raised mid-drain is lost)
  // and runs the handler with it.
  void DrainMessage();

  // Attaches a window and drains anything recorded while there was none.
  void Attach(void* window);

  std::uint32_t pending() const { return pending_.load(); }
  bool has_window() const { return window_ != nullptr; }

 private:
  void DrainNow();

  std::atomic<std::uint32_t> pending_{0};
  std::atomic<bool> post_in_flight_{false};
  void* window_;
  unsigned int drain_message_;
  Handler handler_;
};

}  // namespace platform
