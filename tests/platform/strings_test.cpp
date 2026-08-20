#include "platform/strings.h"

#include <windows.h>
#include <shellapi.h>

#include <string>
#include <vector>

#include "framework/test_case.h"

namespace {

// Builds a command line the way a real caller does, then parses it back with the
// same function the OS gives a child process. This is the whole point of the
// test: QuoteArg is only correct if CommandLineToArgvW is the one confirming it,
// not a second implementation of the same rules that could share a bug.
//
// A dummy program name goes first because argv[0] is parsed by different rules —
// backslashes are never escapes there. Passing a QuoteArg result as argv[0] is
// exactly the mistake the header warns against, so the test does not make it.
std::vector<std::wstring> RoundTrip(const std::vector<std::wstring>& args) {
  std::wstring command_line = L"program.exe";
  for (const std::wstring& arg : args) {
    command_line.push_back(L' ');
    command_line.append(str::QuoteArg(arg));
  }

  int count = 0;
  wchar_t** argv = CommandLineToArgvW(command_line.c_str(), &count);
  if (argv == nullptr) {
    return {};
  }

  std::vector<std::wstring> parsed;
  for (int i = 1; i < count; ++i) {  // skip the program name
    parsed.emplace_back(argv[i]);
  }
  LocalFree(argv);
  return parsed;
}

void CheckRoundTrip(const std::wstring& arg, const char* file, int line) {
  const std::vector<std::wstring> parsed = RoundTrip({arg});
  if (parsed.size() != 1) {
    ::testing::Fail("expected exactly one argument back, got " +
                        ::testing::Describe(static_cast<unsigned long long>(parsed.size())),
                    file, line);
  }
  if (parsed[0] != arg) {
    ::testing::Fail(std::string("round trip changed the argument\n    expected: ") +
                        ::testing::Describe(std::wstring_view(arg)) + "\n    actual:   " +
                        ::testing::Describe(std::wstring_view(parsed[0])),
                    file, line);
  }
}

}  // namespace

#define CHECK_ROUND_TRIP(arg) CheckRoundTrip((arg), __FILE__, __LINE__)

// The hostile table from the issue, plus the cases that motivated the real rules.
// Each entry is here because a naive implementation gets it wrong in a specific
// way, noted alongside.
DHEPZ_TEST(QuoteArg, RoundTripsHostileInputs) {
  CHECK_ROUND_TRIP(L"plain");
  CHECK_ROUND_TRIP(L"");                       // dropped entirely if not emitted as ""
  CHECK_ROUND_TRIP(L"has space");
  CHECK_ROUND_TRIP(L"has\ttab");
  CHECK_ROUND_TRIP(L"C:\\path with space\\");  // trailing \ escapes the close quote
  CHECK_ROUND_TRIP(L"a\"b");                   // bare quote ends the token early
  CHECK_ROUND_TRIP(L"a\\\\");                  // run of backslashes at the end
  CHECK_ROUND_TRIP(L"\"\"");
  CHECK_ROUND_TRIP(L"a\\\"b\\\\");             // both cases in one argument
  CHECK_ROUND_TRIP(L"\\");
  CHECK_ROUND_TRIP(L"\\\\server\\share");      // UNC: leading backslashes are literal
  CHECK_ROUND_TRIP(L"--flag=value with space");
  CHECK_ROUND_TRIP(L"trailing backslash \\");
  CHECK_ROUND_TRIP(L"quote at end\"");
  CHECK_ROUND_TRIP(L"\"quote at start");
  CHECK_ROUND_TRIP(L"embedded\"quote\"pairs");
  CHECK_ROUND_TRIP(L"unicode \u00e9\u4e2d\U0001F600");
}

// Multiple arguments matter separately: a single argument can survive while the
// boundary between two is still wrong, which is how an injected argument appears.
DHEPZ_TEST(QuoteArg, PreservesArgumentBoundaries) {
  const std::vector<std::wstring> args = {
      L"C:\\Program Files\\app\\", L"", L"a\"b", L"--flag=x y", L"\\\\",
  };
  const std::vector<std::wstring> parsed = RoundTrip(args);
  DHEPZ_CHECK_EQ(parsed.size(), args.size());
  for (std::size_t i = 0; i < args.size() && i < parsed.size(); ++i) {
    DHEPZ_CHECK_EQ(parsed[i], args[i]);
  }
}

// The injection case in concrete terms. Without quoting, a folder name ending in
// a quote lets the value close the token and start a new one, so the child sees
// an argument nobody passed.
DHEPZ_TEST(QuoteArg, BlocksArgumentInjection) {
  const std::wstring hostile = L"dir\" --elevate --exec=calc.exe \"";
  const std::vector<std::wstring> parsed = RoundTrip({hostile, L"real-second-arg"});

  DHEPZ_CHECK_EQ(parsed.size(), static_cast<std::size_t>(2));
  if (parsed.size() == 2) {
    DHEPZ_CHECK_EQ(parsed[0], hostile);
    DHEPZ_CHECK_EQ(parsed[1], std::wstring(L"real-second-arg"));
  }
}

// An argument needing no quoting must come back byte-identical rather than
// gratuitously wrapped: callers log these, and a diff in a trace is noise.
DHEPZ_TEST(QuoteArg, LeavesSafeArgumentsUntouched) {
  DHEPZ_CHECK_EQ(str::QuoteArg(L"simple"), std::wstring(L"simple"));
  DHEPZ_CHECK_EQ(str::QuoteArg(L"C:\\no\\spaces"), std::wstring(L"C:\\no\\spaces"));
  DHEPZ_CHECK_EQ(str::QuoteArg(L"a\\b"), std::wstring(L"a\\b"));
  DHEPZ_CHECK_EQ(str::QuoteArg(L"--flag=value"), std::wstring(L"--flag=value"));
}

// The empty argument is quoted rather than skipped. If it vanished, every later
// positional argument would shift down by one.
DHEPZ_TEST(QuoteArg, EmptyArgumentBecomesQuotedPair) {
  DHEPZ_CHECK_EQ(str::QuoteArg(L""), std::wstring(L"\"\""));
}

// Pins the exact backslash-doubling rule, so a future refactor that still passes
// the round-trip by accident cannot quietly change the encoding.
DHEPZ_TEST(QuoteArg, DoublesBackslashesOnlyWhereRequired) {
  // Trailing run doubles: it would otherwise escape the closing quote.
  DHEPZ_CHECK_EQ(str::QuoteArg(L"a b\\"), std::wstring(L"\"a b\\\\\""));
  // Run before a quote doubles, then one more escapes the quote.
  DHEPZ_CHECK_EQ(str::QuoteArg(L"a\\\"b"), std::wstring(L"\"a\\\\\\\"b\""));
  // Interior run with no quote after it stays as written.
  DHEPZ_CHECK_EQ(str::QuoteArg(L"a\\b c"), std::wstring(L"\"a\\b c\""));
}

DHEPZ_TEST(Utf8, RoundTripsAscii) {
  std::string utf8;
  DHEPZ_CHECK(str::ToUtf8(L"hello", &utf8).ok());
  DHEPZ_CHECK_EQ(utf8, std::string("hello"));

  std::wstring wide;
  DHEPZ_CHECK(str::FromUtf8(utf8, &wide).ok());
  DHEPZ_CHECK_EQ(wide, std::wstring(L"hello"));
}

DHEPZ_TEST(Utf8, RoundTripsNonAsciiIncludingAstralPlane) {
  // A surrogate pair, which is where a length-based implementation goes wrong.
  const std::wstring original = L"caf\u00e9 \u4e2d\u6587 \U0001F600";

  std::string utf8;
  DHEPZ_CHECK(str::ToUtf8(original, &utf8).ok());
  std::wstring wide;
  DHEPZ_CHECK(str::FromUtf8(utf8, &wide).ok());
  DHEPZ_CHECK_EQ(wide, original);
}

DHEPZ_TEST(Utf8, EmptyInputSucceedsWithEmptyOutput) {
  std::wstring wide = L"stale";
  DHEPZ_CHECK(str::FromUtf8("", &wide).ok());
  DHEPZ_CHECK(wide.empty());

  std::string utf8 = "stale";
  DHEPZ_CHECK(str::ToUtf8(L"", &utf8).ok());
  DHEPZ_CHECK(utf8.empty());
}

// The behaviour the old build got wrong: invalid input returned an empty string
// indistinguishable from success, so a config file with one bad byte silently
// parsed as empty and the UI came up blank with nothing to explain it.
DHEPZ_TEST(Utf8, InvalidUtf8IsAParseErrorNotEmptyString) {
  const std::string invalid = "\xC3\x28";  // truncated two-byte sequence
  std::wstring wide;
  const core::Status status = str::FromUtf8(invalid, &wide);
  DHEPZ_CHECK_FALSE(status.ok());
  DHEPZ_CHECK_EQ(status.Code(), core::ErrorCode::ParseError);
  DHEPZ_CHECK(wide.empty());
}

DHEPZ_TEST(Utf8, LoneContinuationByteIsRejected) {
  std::wstring wide;
  DHEPZ_CHECK_FALSE(str::FromUtf8("\x80", &wide).ok());
}

DHEPZ_TEST(Utf8, UnpairedSurrogateIsRejected) {
  // A high surrogate with nothing following it has no UTF-8 encoding. A
  // std::wstring can hold it, and so can a filesystem name.
  const std::wstring lone_surrogate(1, static_cast<wchar_t>(0xD800));
  std::string utf8;
  const core::Status status = str::ToUtf8(lone_surrogate, &utf8);
  DHEPZ_CHECK_FALSE(status.ok());
  DHEPZ_CHECK_EQ(status.Code(), core::ErrorCode::ParseError);
  DHEPZ_CHECK(utf8.empty());
}

DHEPZ_TEST(Utf8, NullOutputIsInvalidArgument) {
  DHEPZ_CHECK_EQ(str::FromUtf8("x", nullptr).Code(), core::ErrorCode::InvalidArgument);
  DHEPZ_CHECK_EQ(str::ToUtf8(L"x", nullptr).Code(), core::ErrorCode::InvalidArgument);
}

// A BOM is a zero-width no-break space to a converter, not a marker, so it comes
// through as U+FEFF. Stripping it is the file layer's job (#7), and this pins
// that division so neither layer starts doing it twice.
DHEPZ_TEST(Utf8, BomIsPreservedAsACharacterNotStripped) {
  std::wstring wide;
  DHEPZ_CHECK(str::FromUtf8("\xEF\xBB\xBFtext", &wide).ok());
  DHEPZ_CHECK_EQ(wide, std::wstring(L"\uFEFFtext"));
}

DHEPZ_TEST(Trim, RemovesAsciiWhitespaceFromBothEnds) {
  DHEPZ_CHECK_EQ(str::Trim(L"  padded  "), std::wstring(L"padded"));
  DHEPZ_CHECK_EQ(str::Trim(L"\t\r\n mixed \n\r\t"), std::wstring(L"mixed"));
  DHEPZ_CHECK_EQ(str::Trim(L"none"), std::wstring(L"none"));
  DHEPZ_CHECK_EQ(str::Trim(L""), std::wstring(L""));
  DHEPZ_CHECK_EQ(str::Trim(L"   "), std::wstring(L""));
}

DHEPZ_TEST(Trim, LeavesInteriorWhitespaceAlone) {
  DHEPZ_CHECK_EQ(str::Trim(L"  a b  c  "), std::wstring(L"a b  c"));
}

// Doubling is the whole rule inside a PowerShell single-quoted string: nothing
// else is special there, so escaping a backslash would corrupt every path.
DHEPZ_TEST(EscapeShell, PowerShellDoublesQuotesAndLeavesBackslashesAlone) {
  DHEPZ_CHECK_EQ(str::EscapePowerShellSingleQuoted(L"plain"), std::wstring(L"plain"));
  DHEPZ_CHECK_EQ(str::EscapePowerShellSingleQuoted(L"C:\\Program Files\\app"),
                 std::wstring(L"C:\\Program Files\\app"));
  DHEPZ_CHECK_EQ(str::EscapePowerShellSingleQuoted(L"it's"), std::wstring(L"it''s"));
  DHEPZ_CHECK_EQ(str::EscapePowerShellSingleQuoted(L"''"), std::wstring(L"''''"));
  DHEPZ_CHECK_EQ(str::EscapePowerShellSingleQuoted(L""), std::wstring(L""));
  // The injection shape: without doubling, the quote closes the string and the
  // rest becomes executable script.
  DHEPZ_CHECK_EQ(str::EscapePowerShellSingleQuoted(L"x'; Remove-Item C:\\ -Recurse; '"),
                 std::wstring(L"x''; Remove-Item C:\\ -Recurse; ''"));
}

DHEPZ_TEST(EscapeShell, PosixClosesAndReopensAroundEachQuote) {
  DHEPZ_CHECK_EQ(str::EscapePosixSingleQuoted(L"plain"), std::wstring(L"plain"));
  DHEPZ_CHECK_EQ(str::EscapePosixSingleQuoted(L"it's"), std::wstring(L"it'\\''s"));
  DHEPZ_CHECK_EQ(str::EscapePosixSingleQuoted(L""), std::wstring(L""));
  DHEPZ_CHECK_EQ(str::EscapePosixSingleQuoted(L"'"), std::wstring(L"'\\''"));
  // A backslash is literal inside POSIX single quotes too, so /mnt/c paths pass
  // through unchanged.
  DHEPZ_CHECK_EQ(str::EscapePosixSingleQuoted(L"/mnt/c/a\\b"), std::wstring(L"/mnt/c/a\\b"));
  DHEPZ_CHECK_EQ(str::EscapePosixSingleQuoted(L"x'; rm -rf /; '"),
                 std::wstring(L"x'\\''; rm -rf /; '\\''"));
}
