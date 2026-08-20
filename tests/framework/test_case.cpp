#include "framework/test_case.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>

namespace testing {

std::vector<TestCase>& Registry() {
  static std::vector<TestCase> registry;
  return registry;
}

namespace {

std::string Utf8From(std::wstring_view value) {
  if (value.empty()) {
    return {};
  }
  const int needed = ::WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                           static_cast<int>(value.size()), nullptr, 0, nullptr,
                                           nullptr);
  if (needed <= 0) {
    // Lone surrogates and other unpaired code units land here. A test failure
    // message must still be printable, so say so rather than losing the failure.
    return "<not valid UTF-16>";
  }
  std::string result(static_cast<std::size_t>(needed), '\0');
  ::WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(),
                        needed, nullptr, nullptr);
  return result;
}

std::string Printf(const char* format, ...) {
  char buffer[64] = {};
  va_list args;
  va_start(args, format);
  const int written = std::vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  return written > 0 ? std::string(buffer, static_cast<std::size_t>(written)) : std::string();
}

// Strings are quoted so that trailing whitespace and empty values are visible in
// a failure message. An unquoted empty expected value looks like a broken test.
std::string Quote(std::string_view value) {
  std::string quoted;
  quoted.reserve(value.size() + 2);
  quoted += '"';
  quoted += value;
  quoted += '"';
  return quoted;
}

}  // namespace

std::string Describe(bool value) { return value ? "true" : "false"; }
std::string Describe(int value) { return Printf("%d", value); }
std::string Describe(long long value) { return Printf("%lld", value); }
std::string Describe(unsigned long long value) { return Printf("%llu", value); }
std::string Describe(double value) { return Printf("%.17g", value); }
std::string Describe(const char* value) { return value ? Quote(value) : "<nullptr>"; }
std::string Describe(const wchar_t* value) {
  return value ? Quote(Utf8From(value)) : "<nullptr>";
}
std::string Describe(std::string_view value) { return Quote(value); }
std::string Describe(std::wstring_view value) { return Quote(Utf8From(value)); }

void Fail(std::string message, const char* file, int line) {
  throw AssertionFailure(std::move(message), file ? file : "<unknown>", line);
}

}  // namespace testing
