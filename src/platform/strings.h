// String helpers, and the command-line quoting that guards every process launch.
//
// QuoteArg is the security-relevant part of this header. Windows has no argv:
// CreateProcess takes one flat string, and the child pulls arguments back out of
// it with CommandLineToArgvW. That means anything appended into a command line is
// parsed by the child, so a folder name containing a quote is not a display bug,
// it is an injected argument. Once directory names reach a command line from JSON
// config or a user-typed path, naive concatenation is a command-injection vector.
//
// Unlike core/, this layer is allowed to know about Windows — but the knowledge
// stays in the .cpp so callers do not transitively pull in windows.h.
#pragma once

#include <string>
#include <string_view>

#include "core/status.h"

namespace str {

// UTF-8 to UTF-16, strictly. Invalid or truncated UTF-8 is a ParseError rather
// than a silently empty string.
//
// The strictness is the point. The old build returned {} on failure, so a config
// file with one bad byte parsed as an empty document and the UI just came up
// blank with nothing to explain it. core/json (#6) needs a real error here to
// report a line and column.
core::Status FromUtf8(std::string_view utf8, std::wstring* out);

// UTF-16 to UTF-8. Fails on unpaired surrogates, which have no UTF-8 encoding.
// A std::wstring can hold them, so this is reachable from a filesystem name.
core::Status ToUtf8(std::wstring_view text, std::string* out);

// Strips leading and trailing space, tab, CR and LF. Not locale-aware, and
// deliberately not Unicode-whitespace-aware: it is used on config values and
// path strings, where the only realistic input is ASCII whitespace from hand
// editing.
std::wstring Trim(std::wstring_view text);

// Wraps one argument so CommandLineToArgvW recovers it exactly, implementing the
// real rules rather than a plausible approximation:
//
//   - Backslashes are only special immediately before a quote. `a\b` needs no
//     escaping at all, but `a\"` needs the backslash doubled, because otherwise
//     the child sees an escaped quote and the token never terminates.
//   - A run of backslashes at the very end of a quoted argument is doubled, or
//     the last one would escape the closing quote.
//   - An empty argument must still appear, as `""`. Dropping it shifts every
//     later positional argument by one.
//
// Round-trip guarantee, covered by tests: CommandLineToArgvW applied to a
// command line built from QuoteArg output returns the original strings exactly.
//
// NOT valid for argv[0]. The program-name token at the front of a command line
// is parsed by different rules — backslashes are never escapes there, since the
// token is a path, and quotes merely toggle quoting. Quoting a path with
// QuoteArg and placing it first would corrupt any path containing a backslash
// before a quote. Pass the executable to CreateProcess as lpApplicationName, or
// wrap it in plain quotes.
std::wstring QuoteArg(std::wstring_view value);

// Escapes a value for interpolation inside a PowerShell single-quoted string.
// Only the quote itself is special there — no backslash escapes, no expansion —
// so doubling it is the complete rule.
//
// The caller still supplies the surrounding quotes. That is deliberate: a helper
// that added them would look safe when used to build an unquoted fragment, which
// is the one case where it does nothing.
std::wstring EscapePowerShellSingleQuoted(std::wstring_view value);

// Escapes a value for interpolation inside a POSIX shell single-quoted string,
// for the WSL path. A single-quoted string cannot contain its own quote at all,
// so the only encoding is to close, emit an escaped quote, and reopen: '\''.
//
// Same contract as above: the caller supplies the outer quotes.
std::wstring EscapePosixSingleQuoted(std::wstring_view value);

}  // namespace str
