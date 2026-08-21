// WSL distro enumeration (P4-03): `wsl -l -q` through an injected runner
// (process::RunCapture by default, a mock in tests), cached per session,
// refreshable explicitly. Adding a distro on the machine shows up after
// Refresh — no rebuild, no code change.
#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "core/status.h"
#include "platform/process.h"

namespace terminal {

class WslEnumerator final {
 public:
  using Runner =
      std::function<core::Status(std::wstring_view command_line, process::RunResult* out)>;

  // Default runner: process::RunCapture with a 10 s deadline. Callers run
  // this off the UI thread (worker offload, P4-05).
  WslEnumerator();
  explicit WslEnumerator(Runner runner);

  // Runs `wsl -l -q`, parses, replaces the cache. A failed run keeps the
  // previous cache and returns the Status.
  core::Status Refresh();

  // Session cache; empty until the first successful Refresh.
  const std::vector<std::wstring>& Distros() const { return distros_; }
  bool cached() const { return cached_; }

  // Pure parse, exposed for tests: tolerates UTF-16-decoded input, skips
  // the header line, blank lines, and "(Default)" markers.
  static std::vector<std::wstring> ParseListOutput(const std::wstring& output);

 private:
  Runner runner_;
  std::vector<std::wstring> distros_;
  bool cached_ = false;
};

}  // namespace terminal
