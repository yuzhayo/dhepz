#include "platform/signal_fanout.h"

#include <windows.h>

#include <utility>

namespace platform {

SignalFanout::SignalFanout(void* window, unsigned int drain_message, Handler handler)
    : window_(window), drain_message_(drain_message), handler_(std::move(handler)) {}

void SignalFanout::Raise(OsSignal signal) {
  pending_.fetch_or(static_cast<std::uint32_t>(signal));
  if (window_ == nullptr) {
    return;  // recorded; applied on attach
  }
  if (post_in_flight_.exchange(true)) {
    return;  // this burst already has its one message
  }
  if (PostMessageW(static_cast<HWND>(window_), drain_message_, 0, 0)) {
    return;
  }
  // The post failed: do the work inline rather than dropping the signal.
  post_in_flight_.store(false);
  DrainNow();
}

void SignalFanout::DrainMessage() {
  // The message consumed the in-flight marker; take-and-clear the mask so a
  // raise during the handler starts a fresh post instead of vanishing.
  post_in_flight_.store(false);
  DrainNow();
}

void SignalFanout::DrainNow() {
  const std::uint32_t signals = pending_.exchange(0);
  if (signals != 0 && handler_) {
    handler_(signals);
  }
}

void SignalFanout::Attach(void* window) {
  window_ = window;
  if (window_ != nullptr && pending_.load() != 0) {
    DrainNow();
  }
}

}  // namespace platform
