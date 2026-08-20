#include "platform/settle_timer.h"

#include <windows.h>

#include <utility>

namespace platform {

SettleTimer::SettleTimer(void* window, std::uintptr_t timer_id, Callback on_settled)
    : window_(window), timer_id_(timer_id), on_settled_(std::move(on_settled)) {}

SettleTimer::~SettleTimer() { Kill(); }

void SettleTimer::Arm(unsigned int delay_ms) {
  if (window_ == nullptr) return;
  if (armed_) {
    Kill();  // re-arming resets the countdown; timers never stack
  }
  armed_ = SetTimer(static_cast<HWND>(window_), timer_id_, delay_ms, nullptr) != 0;
}

void SettleTimer::OnTimer() {
  // Kill FIRST, before any work: one-shot by construction, not by hoping
  // the callback logic reaches the kill.
  Kill();
  if (on_settled_) {
    on_settled_();
  }
}

void SettleTimer::Kill() {
  if (armed_ && window_ != nullptr) {
    KillTimer(static_cast<HWND>(window_), timer_id_);
  }
  armed_ = false;
}

}  // namespace platform
