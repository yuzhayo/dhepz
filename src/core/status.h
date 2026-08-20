// The error type every layer returns.
//
// Two things are deliberately separate here. The `ErrorCode` is stable and is
// what callers branch on; the message is human-readable and its wording is free
// to change. The old build welded a status string to a repaint decision inside
// six different screens, so the text and the "now redraw" call could not move
// independently. Here the text stays in the Status and the frontend decides how
// and when to show it.
//
// Success must be free. A default-constructed Status holds an empty
// std::wstring, which lives in the small-string buffer, so the happy path
// performs no allocation at all. That matters because Status is the return type
// of every operation in the process, including ones on the UI thread (G2).
//
// core/ never includes Windows.h. This header is <string> only.
#pragma once

#include <string>
#include <string_view>
#include <utility>

namespace core {

// Stable codes for caller branching. Append to the end; never renumber, and
// never reuse a retired value, because module code compiled against an older
// header would silently mean something different.
enum class ErrorCode {
  Ok = 0,
  InvalidArgument,
  NotFound,
  AlreadyExists,
  IoError,
  ParseError,
  PermissionDenied,
  Unsupported,
  Cancelled,
  Internal,
};

// Short, stable, non-localised identifier for a code. This is for traces and
// test failure output, not for the UI: user-facing wording belongs in the
// Status message so it can be worded per call site.
inline const wchar_t* ErrorCodeName(ErrorCode code) {
  switch (code) {
    case ErrorCode::Ok:
      return L"Ok";
    case ErrorCode::InvalidArgument:
      return L"InvalidArgument";
    case ErrorCode::NotFound:
      return L"NotFound";
    case ErrorCode::AlreadyExists:
      return L"AlreadyExists";
    case ErrorCode::IoError:
      return L"IoError";
    case ErrorCode::ParseError:
      return L"ParseError";
    case ErrorCode::PermissionDenied:
      return L"PermissionDenied";
    case ErrorCode::Unsupported:
      return L"Unsupported";
    case ErrorCode::Cancelled:
      return L"Cancelled";
    case ErrorCode::Internal:
      return L"Internal";
  }
  // Reached only if a caller casts an out-of-range integer into the enum.
  return L"Unknown";
}

// [[nodiscard]] is on the type rather than on individual functions, so every
// function anywhere that returns a Status inherits it. Combined with /WX, a
// dropped error is a build failure rather than a silent one.
class [[nodiscard]] Status {
 public:
  // Success. Trivial, allocation-free, and the common case.
  Status() = default;

  Status(ErrorCode code, std::wstring message)
      : code_(code), message_(std::move(message)) {}

  ErrorCode Code() const { return code_; }
  const std::wstring& Message() const { return message_; }

  bool ok() const { return code_ == ErrorCode::Ok; }

  // Explicit so that `if (status)` reads correctly while an accidental
  // comparison or arithmetic on a Status does not compile.
  explicit operator bool() const { return ok(); }

  // Attaches the source location to the message. Config diagnostics (G3) have
  // to say which file and line produced a complaint, and a module author needs
  // that to be automatic rather than remembered. Only ever called on error
  // paths, so the allocation here is not on any hot path.
  //
  // Prefer the DHEPZ_ERR macro below over calling this directly.
  Status& AddContext(std::wstring_view file, int line) {
    if (ok()) {
      return *this;
    }
    message_.append(L" [").append(Basename(file)).append(1, L':');
    message_.append(std::to_wstring(line)).append(1, L']');
    return *this;
  }

 private:
  // __FILEW__ expands to the path as written on the compiler command line,
  // which is long and machine-specific. Only the leaf is useful to a reader.
  static std::wstring_view Basename(std::wstring_view path) {
    const std::size_t cut = path.find_last_of(L"\\/");
    return cut == std::wstring_view::npos ? path : path.substr(cut + 1);
  }

  ErrorCode code_ = ErrorCode::Ok;
  std::wstring message_;
};

inline Status Ok() { return Status(); }

inline Status Err(ErrorCode code, std::wstring message) {
  return Status(code, std::move(message));
}

// The normal way to produce an error: stamps file and line automatically.
//
//   return DHEPZ_ERR(ErrorCode::NotFound, L"No screen named " + name);
#define DHEPZ_ERR(code, message) \
  (::core::Err((code), (message)).AddContext(__FILEW__, __LINE__))

// Propagates a failing Status out of the current function, keeping the
// original code, message, and context. Wrapped in do/while so it behaves like
// a statement after an `if` without braces.
#define DHEPZ_RETURN_IF_ERROR(expr)          \
  do {                                       \
    ::core::Status dhepz_status__ = (expr);  \
    if (!dhepz_status__) {                   \
      return dhepz_status__;                 \
    }                                        \
  } while (false)

}  // namespace core
