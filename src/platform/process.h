// Process launch + capture (P4-02), ported from the old build's
// logic/platform/process.* and converted to core::Status.
//
// Launches open EXTERNAL console windows (the old model; no in-app conpty).
// RunCapture is bounded and deadline-aware: a child that keeps the pipe open
// cannot hang the caller past timeout_ms, and all arguments the callers
// build must go through str::QuoteArg (the module side does; the tests
// prove the round trip).
#pragma once

#include <string>
#include <string_view>

#include "core/status.h"

namespace process {

enum class WindowMode { NewConsole, Hidden };

struct RunResult {
  int exit_code = 0;
  std::wstring output;  // decoded: UTF-16LE sniffed, else UTF-8
  bool timed_out = false;
};

// Detached launch; the child owns its console.
core::Status Launch(std::wstring_view exe, std::wstring_view command_line,
                    std::wstring_view working_dir, WindowMode window);

// ShellExecuteExW so verbs like "runas" (UAC elevation) work. A cancelled
// elevation prompt is ErrorCode::Cancelled, not a crash.
core::Status ShellLaunch(std::wstring_view verb, std::wstring_view file,
                         std::wstring_view parameters, std::wstring_view working_dir);

// Runs hidden, captures stdout+stderr (merged), bounded by timeout_ms.
// Timeout is reported in the result (timed_out) with the child terminated;
// spawn failures are a Status error. Never blocks past the deadline.
core::Status RunCapture(std::wstring_view command_line, std::wstring_view working_dir,
                        unsigned long timeout_ms, RunResult* out);

// FormatMessageW for humans.
std::wstring ErrorMessage(unsigned long code);

}  // namespace process
