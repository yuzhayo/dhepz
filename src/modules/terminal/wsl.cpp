#include "modules/terminal/wsl.h"

namespace terminal {
namespace {

core::Status DefaultRunner(std::wstring_view command_line, process::RunResult* out) {
  return process::RunCapture(command_line, L"", 10000, out);
}

}  // namespace

WslEnumerator::WslEnumerator() : WslEnumerator(&DefaultRunner) {}

WslEnumerator::WslEnumerator(Runner runner) : runner_(std::move(runner)) {}

std::vector<std::wstring> WslEnumerator::ParseListOutput(const std::wstring& output) {
  std::vector<std::wstring> distros;
  std::wstring line;
  bool first_content = true;
  const auto flush = [&] {
    std::wstring name = line;
    // strip \r and surrounding whitespace
    while (!name.empty() && (name.back() == L'\r' || name.back() == L' ')) name.pop_back();
    while (!name.empty() && (name.front() == L' ')) name.erase(name.begin());
    // remove a trailing " (Default)" marker wherever it sits
    const std::wstring marker = L"(Default)";
    const auto pos = name.find(marker);
    if (pos != std::wstring::npos) {
      name.erase(pos);
      while (!name.empty() && (name.back() == L' ')) name.pop_back();
    }
    if (first_content) {
      first_content = false;  // the header line ("Windows Subsystem ...")
      return;
    }
    if (!name.empty()) distros.push_back(name);
  };
  for (const wchar_t c : output) {
    if (c == L'\n') {
      flush();
      line.clear();
    } else {
      line.push_back(c);
    }
  }
  if (!line.empty()) flush();
  return distros;
}

core::Status WslEnumerator::Refresh() {
  process::RunResult result;
  const core::Status status = runner_(L"wsl.exe -l -q", &result);
  if (!status.ok()) return status;
  if (result.timed_out) {
    return core::Err(core::ErrorCode::Cancelled, L"wsl enumeration timed out");
  }
  distros_ = ParseListOutput(result.output);
  cached_ = true;
  return core::Ok();
}

}  // namespace terminal
