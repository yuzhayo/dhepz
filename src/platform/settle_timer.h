// The settle timer: measures "time until things settle after activity"
// without violating G1's no-timers-at-idle rule.
//
// The contradiction this resolves: settle-time measurement needs a timer,
// and G1 forbids timers that tick when nothing changed. The answer is a
// timer that only exists while something is happening:
//
//   - SetTimer is called ONLY in response to real activity (resize, DPI
//     change, animation). Idle means no timer object exists at all.
//   - The handler calls KillTimer as its FIRST action, before doing any
//     work — the timer is one-shot by construction, not by hoping the logic
//     reaches the kill.
//   - Re-arming during ongoing activity kills and re-sets, so a burst of
//     activity yields one settle, never a stack of timers.
//
// This header stays free of windows.h.
#pragma once

#include <cstdint>
#include <functional>

namespace platform {

class SettleTimer final {
 public:
  using Callback = std::function<void()>;

  // `timer_id` distinguishes this timer in WM_TIMER; the window proc routes
  // matching WM_TIMERs to OnTimer().
  SettleTimer(void* window, std::uintptr_t timer_id, Callback on_settled);
  ~SettleTimer();

  SettleTimer(const SettleTimer&) = delete;
  SettleTimer& operator=(const SettleTimer&) = delete;

  // Arms in response to activity. Already armed: the countdown resets
  // (kill + set), timers never stack.
  void Arm(unsigned int delay_ms);

  bool armed() const { return armed_; }

  // Window proc entry: kills the timer first, then runs the callback.
  void OnTimer();

 private:
  void Kill();

  void* window_;
  std::uintptr_t timer_id_;
  Callback on_settled_;
  bool armed_ = false;
};

}  // namespace platform
