#include "modules/terminal/wsl.h"

#include <algorithm>
#include <cwctype>

namespace terminal {
namespace {

core::Status DefaultRunner(std::wstring_view command_line, process::RunResult* out) {
  return process::RunCapture(command_line, L"", 10000, out);
}

}  // namespace

namespace {

std::wstring TrimLine(std::wstring line) {
  line.erase(std::remove(line.begin(), line.end(), L'\0'), line.end());
  while (!line.empty() && (std::iswspace(line.back()) || line.back() == L'\uFEFF')) {
    line.pop_back();
  }
  std::size_t first = 0;
  while (first < line.size() && (std::iswspace(line[first]) || line[first] == L'\uFEFF')) {
    ++first;
  }
  line.erase(0, first);
  return line;
}

bool IsRecognizedHeader(const std::wstring& line) {
  return line == L"Windows Subsystem for Linux Distributions:" ||
         line == L"Windows Subsystem for Linux Distributions";
}

}  // namespace

WslEnumerator::WslEnumerator() : WslEnumerator(&DefaultRunner) {}

WslEnumerator::WslEnumerator(Runner runner) : runner_(std::move(runner)) {}

std::vector<std::wstring> WslEnumerator::ParseListOutput(const std::wstring& output) {
  std::vector<std::wstring> distros;
  std::wstring line;
  const auto flush = [&] {
    std::wstring name = TrimLine(line);
    if (name.empty() || IsRecognizedHeader(name)) return;

    constexpr std::wstring_view marker = L" (Default)";
    if (name.size() >= marker.size() &&
        name.compare(name.size() - marker.size(), marker.size(), marker) == 0) {
      name.erase(name.size() - marker.size());
      name = TrimLine(std::move(name));
    }
    if (!name.empty()) distros.push_back(std::move(name));
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
