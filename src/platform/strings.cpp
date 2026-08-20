#include "platform/strings.h"

#include <windows.h>

#include <string>

namespace str {
namespace {

// MultiByteToWideChar and its counterpart take int lengths. A string longer than
// INT_MAX is not something this app produces, but the cast is unchecked
// otherwise and /W4 is right to care.
core::Status CheckLength(std::size_t size, const wchar_t* what) {
  if (size > static_cast<std::size_t>(INT_MAX)) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                     std::wstring(what) + L" exceeds the maximum convertible length");
  }
  return core::Ok();
}

}  // namespace

core::Status FromUtf8(std::string_view utf8, std::wstring* out) {
  if (out == nullptr) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument, L"FromUtf8 requires an output string");
  }
  out->clear();
  if (utf8.empty()) {
    return core::Ok();
  }
  DHEPZ_RETURN_IF_ERROR(CheckLength(utf8.size(), L"UTF-8 input"));

  const int size = static_cast<int>(utf8.size());
  // MB_ERR_INVALID_CHARS is what makes this strict. Without it, an invalid byte
  // is replaced by U+FFFD and the caller cannot tell a corrupt config from a
  // valid one that happens to contain a replacement character.
  const int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), size, nullptr, 0);
  if (needed <= 0) {
    return DHEPZ_ERR(core::ErrorCode::ParseError, L"Input is not valid UTF-8");
  }

  out->resize(static_cast<std::size_t>(needed));
  const int written =
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), size, out->data(), needed);
  if (written != needed) {
    out->clear();
    return DHEPZ_ERR(core::ErrorCode::Internal, L"UTF-8 conversion length changed between calls");
  }
  return core::Ok();
}

core::Status ToUtf8(std::wstring_view text, std::string* out) {
  if (out == nullptr) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument, L"ToUtf8 requires an output string");
  }
  out->clear();
  if (text.empty()) {
    return core::Ok();
  }
  DHEPZ_RETURN_IF_ERROR(CheckLength(text.size(), L"UTF-16 input"));

  const int size = static_cast<int>(text.size());
  // WC_ERR_INVALID_CHARS rejects unpaired surrogates, which have no UTF-8
  // encoding. A std::wstring can hold one, and a filesystem name can contain
  // one, so this path is reachable rather than theoretical.
  const int needed =
      WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), size, nullptr, 0, nullptr, nullptr);
  if (needed <= 0) {
    return DHEPZ_ERR(core::ErrorCode::ParseError,
                     L"Text contains an unpaired surrogate and cannot be encoded as UTF-8");
  }

  out->resize(static_cast<std::size_t>(needed));
  const int written = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), size, out->data(),
                                          needed, nullptr, nullptr);
  if (written != needed) {
    out->clear();
    return DHEPZ_ERR(core::ErrorCode::Internal, L"UTF-16 conversion length changed between calls");
  }
  return core::Ok();
}

std::wstring Trim(std::wstring_view text) {
  const auto is_space = [](wchar_t c) {
    return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n';
  };
  std::size_t begin = 0;
  std::size_t end = text.size();
  while (begin < end && is_space(text[begin])) {
    ++begin;
  }
  while (end > begin && is_space(text[end - 1])) {
    --end;
  }
  return std::wstring(text.substr(begin, end - begin));
}

std::wstring QuoteArg(std::wstring_view value) {
  // CommandLineToArgvW splits only on space and tab. CR, LF and vertical tab are
  // included here anyway: they cannot appear in an argument that survives a
  // round trip through a shell, and quoting them costs two characters while
  // leaving them bare risks a caller that does route through cmd.exe.
  const bool needs_quotes = value.empty() || value.find_first_of(L" \t\n\v\r\"") != std::wstring_view::npos;
  if (!needs_quotes) {
    return std::wstring(value);
  }

  std::wstring out;
  // Worst case is every character a backslash immediately before the close
  // quote, which doubles the body: 2n + 2 for the quotes.
  out.reserve(value.size() * 2 + 2);
  out.push_back(L'"');

  for (std::size_t i = 0; i < value.size(); ++i) {
    // Backslashes are only escapes when a quote follows, so count the run first
    // and decide what it means afterwards.
    std::size_t backslashes = 0;
    while (i < value.size() && value[i] == L'\\') {
      ++backslashes;
      ++i;
    }

    if (i == value.size()) {
      // The run ends the argument. Double it, or the final backslash would
      // escape the closing quote and swallow the next argument into this one.
      out.append(backslashes * 2, L'\\');
      break;
    }

    if (value[i] == L'"') {
      // Double the run so it survives as literal backslashes, then one more to
      // escape the quote itself.
      out.append(backslashes * 2 + 1, L'\\');
    } else {
      // Nothing special follows, so the backslashes are literal as written.
      out.append(backslashes, L'\\');
    }
    out.push_back(value[i]);
  }

  out.push_back(L'"');
  return out;
}

std::wstring EscapePowerShellSingleQuoted(std::wstring_view value) {
  std::wstring out;
  out.reserve(value.size());
  for (wchar_t c : value) {
    out.push_back(c);
    if (c == L'\'') {
      out.push_back(L'\'');
    }
  }
  return out;
}

std::wstring EscapePosixSingleQuoted(std::wstring_view value) {
  std::wstring out;
  out.reserve(value.size());
  for (wchar_t c : value) {
    if (c == L'\'') {
      // Close the string, emit a backslash-escaped quote outside it, reopen.
      out.append(L"'\\''");
    } else {
      out.push_back(c);
    }
  }
  return out;
}

}  // namespace str
