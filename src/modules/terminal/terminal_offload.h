// Worker offload for the terminal module (P4-05): blocking work (RunCapture,
// validation, enumeration) runs on worker::Worker threads; results travel
// back as messages to the UI window and never touch UI state directly.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "platform/process.h"
#include "platform/worker.h"

namespace terminal {

class TerminalOffload final {
 public:
  TerminalOffload(void* ui_window, unsigned int completion_message)
      : worker_(ui_window, completion_message) {}

  using OnCapture = std::function<void(const process::RunResult&)>;

  // The work lambda blocks (RunCapture); the delivery runs on the UI thread
  // when the window routes the completion message to Settle().
  worker::JobHandle RunCaptureAsync(std::wstring command_line, OnCapture on_done,
                                    std::uint64_t generation = 0) {
    return worker_.Submit(
        [command_line = std::move(command_line)](const std::atomic<bool>&) {
          auto result = std::make_shared<process::RunResult>();
          const core::Status status = process::RunCapture(command_line, L"", 10000, result.get());
          (void)status;  // the RunResult carries timed_out/exit_code to the UI
          return std::static_pointer_cast<void>(result);
        },
        [on_done = std::move(on_done)](std::shared_ptr<void> cargo) {
          on_done(*std::static_pointer_cast<process::RunResult>(cargo));
        },
        generation);
  }

  worker::Worker& worker() { return worker_; }

 private:
  worker::Worker worker_;
};

}  // namespace terminal
