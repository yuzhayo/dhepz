// The test runner.
//
// No GoogleTest and no external dependency: Velopack stays the only third-party
// runtime dependency, and the test suite is not allowed to add a second one.
//
// Console subsystem, unlike the app itself — this one is meant to be run from a
// shell and read.
#include <windows.h>

#include <crtdbg.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "framework/test_case.h"

namespace {

using Clock = std::chrono::steady_clock;

struct Outcome {
  bool passed = true;
  std::string message;
  std::string file;
  int line = 0;
};

struct Options {
  std::string filter = "*";
  std::filesystem::path junit;
  std::filesystem::path json;
  bool list_only = false;
};

std::string Utf8From(std::wstring_view value) {
  if (value.empty()) {
    return {};
  }
  const int needed = ::WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                           static_cast<int>(value.size()), nullptr, 0, nullptr,
                                           nullptr);
  if (needed <= 0) {
    return {};
  }
  std::string result(static_cast<std::size_t>(needed), '\0');
  ::WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(),
                        needed, nullptr, nullptr);
  return result;
}

// Iterative `*` / `?` glob with backtracking. Recursion on a pattern is a stack
// risk on hostile input, and this is cheap enough not to need it.
bool GlobMatches(std::string_view pattern, std::string_view value) {
  std::size_t p = 0;
  std::size_t v = 0;
  std::size_t star = std::string_view::npos;
  std::size_t star_v = 0;

  while (v < value.size()) {
    if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == value[v])) {
      ++p;
      ++v;
    } else if (p < pattern.size() && pattern[p] == '*') {
      star = p++;
      star_v = v;
    } else if (star != std::string_view::npos) {
      p = star + 1;
      v = ++star_v;
    } else {
      return false;
    }
  }
  while (p < pattern.size() && pattern[p] == '*') {
    ++p;
  }
  return p == pattern.size();
}

// A bare pattern with no wildcard is treated as a substring match, because
// `--filter json` is what anyone actually types. An explicit pattern is honoured
// as written.
bool Selected(std::string_view pattern, std::string_view name) {
  if (pattern.find('*') == std::string_view::npos &&
      pattern.find('?') == std::string_view::npos) {
    return name.find(pattern) != std::string_view::npos;
  }
  return GlobMatches(pattern, name);
}

// Separate from the SEH guard below: a function containing __try/__except cannot
// also require object unwinding, and this one is full of std::string.
void RunCatchingCpp(const testing::TestCase& test, Outcome* out) {
  try {
    test.run();
  } catch (const testing::AssertionFailure& failure) {
    out->passed = false;
    out->message = failure.message();
    out->file = failure.file();
    out->line = failure.line();
  } catch (const std::exception& error) {
    out->passed = false;
    out->message = std::string("unexpected C++ exception: ") + error.what();
  } catch (...) {
    out->passed = false;
    out->message = "unexpected non-std C++ exception";
  }
}

// Catches access violations, divide-by-zero, stack overflow and friends, so a
// crashing test is one reported failure rather than a silent truncated run. No
// locals with destructors here, or MSVC rejects __try (C2712).
unsigned long RunSehGuarded(const testing::TestCase& test, Outcome* out) {
  __try {
    RunCatchingCpp(test, out);
    return 0;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return ::GetExceptionCode();
  }
}

std::string EscapeXml(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (const char c : value) {
    switch (c) {
      case '&': escaped += "&amp;"; break;
      case '<': escaped += "&lt;"; break;
      case '>': escaped += "&gt;"; break;
      case '"': escaped += "&quot;"; break;
      case '\'': escaped += "&apos;"; break;
      default:
        // Control characters other than tab/LF/CR are illegal in XML 1.0 even
        // when escaped, and one of them makes the whole report unparseable —
        // which would lose every result, not just the offending one.
        if (static_cast<unsigned char>(c) < 0x20 && c != '\t' && c != '\n' && c != '\r') {
          escaped += '?';
        } else {
          escaped += c;
        }
        break;
    }
  }
  return escaped;
}

std::string EscapeJson(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (const char c : value) {
    switch (c) {
      case '"': escaped += "\\\""; break;
      case '\\': escaped += "\\\\"; break;
      case '\n': escaped += "\\n"; break;
      case '\r': escaped += "\\r"; break;
      case '\t': escaped += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buffer[8] = {};
          std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned char>(c));
          escaped += buffer;
        } else {
          escaped += c;
        }
        break;
    }
  }
  return escaped;
}

struct Report {
  std::string name;
  std::string classname;
  std::string file;
  int line = 0;
  bool passed = true;
  std::string message;
  double duration_ms = 0.0;
};

std::string SplitSuite(const std::string& name) {
  const std::size_t dot = name.find('.');
  return dot == std::string::npos ? std::string("dhepz") : name.substr(0, dot);
}

// __FILE__ is absolute because MSBuild passes absolute paths to cl. GitHub
// Actions only turns a JUnit failure into an inline annotation when the path is
// repo-relative with forward slashes, so an absolute path silently produces a
// red check with no annotation on the offending line.
//
// Cutting at the last `\tests\` or `\src\` segment rather than stripping a
// baked-in source-root define: a quoted string define does not survive MSBuild
// reliably (see the VERSIONINFO finding in issue #1), and this needs no build
// plumbing to stay correct.
std::string RepoRelative(std::string path) {
  for (char& c : path) {
    if (c == '\\') {
      c = '/';
    }
  }
  for (const std::string_view root : {"/tests/", "/src/"}) {
    const std::size_t at = path.rfind(root);
    if (at != std::string::npos) {
      return path.substr(at + 1);
    }
  }
  return path;
}

bool WriteFile(const std::filesystem::path& path, std::string_view text) {
  std::error_code ec;
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path(), ec);
  }
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    std::fprintf(stderr, "Cannot write report: %s\n", path.string().c_str());
    return false;
  }
  out.write(text.data(), static_cast<std::streamsize>(text.size()));
  return out.good();
}

std::string BuildJunit(const std::vector<Report>& reports, double total_ms) {
  const std::size_t failures = static_cast<std::size_t>(
      std::count_if(reports.begin(), reports.end(), [](const Report& r) { return !r.passed; }));

  std::string xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
  xml += "<testsuites tests=\"" + std::to_string(reports.size()) + "\" failures=\"" +
         std::to_string(failures) + "\" time=\"" + std::to_string(total_ms / 1000.0) + "\">\n";
  xml += "  <testsuite name=\"dhepz\" tests=\"" + std::to_string(reports.size()) +
         "\" failures=\"" + std::to_string(failures) + "\" time=\"" +
         std::to_string(total_ms / 1000.0) + "\">\n";
  for (const Report& r : reports) {
    // file and line go on the <testcase>, which is what lets GitHub Actions turn
    // a failure into an inline annotation on the offending source line.
    xml += "    <testcase classname=\"" + EscapeXml(r.classname) + "\" name=\"" +
           EscapeXml(r.name) + "\" file=\"" + EscapeXml(r.file) + "\" line=\"" +
           std::to_string(r.line) + "\" time=\"" + std::to_string(r.duration_ms / 1000.0) + "\"";
    if (r.passed) {
      xml += "/>\n";
    } else {
      xml += ">\n      <failure message=\"" + EscapeXml(r.message) + "\">" +
             EscapeXml(r.message) + "</failure>\n    </testcase>\n";
    }
  }
  xml += "  </testsuite>\n</testsuites>\n";
  return xml;
}

std::string BuildJson(const std::vector<Report>& reports, double total_ms) {
  const std::size_t failures = static_cast<std::size_t>(
      std::count_if(reports.begin(), reports.end(), [](const Report& r) { return !r.passed; }));

  std::string json = "{\n  \"schemaVersion\": 1,\n";
  json += "  \"totals\": { \"selected\": " + std::to_string(reports.size()) + ", \"passed\": " +
          std::to_string(reports.size() - failures) + ", \"failed\": " +
          std::to_string(failures) + " },\n";
  json += "  \"durationMs\": " + std::to_string(total_ms) + ",\n  \"tests\": [\n";
  for (std::size_t i = 0; i < reports.size(); ++i) {
    const Report& r = reports[i];
    json += "    { \"name\": \"" + EscapeJson(r.classname + "." + r.name) + "\", \"status\": \"" +
            (r.passed ? "passed" : "failed") + "\", \"durationMs\": " +
            std::to_string(r.duration_ms) + ", \"file\": \"" + EscapeJson(r.file) +
            "\", \"line\": " + std::to_string(r.line) + ", \"message\": \"" +
            EscapeJson(r.message) + "\" }";
    json += (i + 1 == reports.size()) ? "\n" : ",\n";
  }
  json += "  ]\n}\n";
  return json;
}

void PrintUsage() {
  std::fprintf(stderr,
               "Usage: dhepz_tests.exe [--filter <pattern>] [--junit <path>] [--json <path>]\n"
               "                       [--list]\n"
               "\n"
               "  --filter  Substring match, or a glob when it contains * or ?.\n"
               "  --junit   Write JUnit XML for CI annotations.\n"
               "  --json    Write a JSON report for tooling.\n"
               "  --list    Print the selected test names and exit without running.\n");
}

bool ParseOptions(int argc, wchar_t** argv, Options* options) {
  for (int i = 1; i < argc; ++i) {
    const std::wstring_view arg = argv[i];
    const bool needs_value = arg == L"--filter" || arg == L"--junit" || arg == L"--json";
    if (needs_value && i + 1 >= argc) {
      std::fprintf(stderr, "%s requires a value.\n", Utf8From(arg).c_str());
      return false;
    }
    if (arg == L"--filter") {
      options->filter = Utf8From(argv[++i]);
    } else if (arg == L"--junit") {
      options->junit = argv[++i];
    } else if (arg == L"--json") {
      options->json = argv[++i];
    } else if (arg == L"--list") {
      options->list_only = true;
    } else {
      std::fprintf(stderr, "Unknown argument: %s\n", Utf8From(arg).c_str());
      return false;
    }
  }
  return true;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  Options options;
  if (!ParseOptions(argc, argv, &options)) {
    PrintUsage();
    return 2;
  }

  // A crash must be a reported failure, not a modal dialog that hangs CI until
  // the job times out.
  ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);

  std::vector<testing::TestCase>& all = testing::Registry();

  // Registration order is link order, which is not stable across incremental
  // builds. Sorting makes a run reproducible and the output diffable.
  std::sort(all.begin(), all.end(),
            [](const testing::TestCase& a, const testing::TestCase& b) { return a.name < b.name; });

  std::vector<const testing::TestCase*> selected;
  for (const testing::TestCase& test : all) {
    if (Selected(options.filter, test.name)) {
      selected.push_back(&test);
    }
  }

  if (options.list_only) {
    for (const testing::TestCase* test : selected) {
      std::printf("%s\n", test->name.c_str());
    }
    return selected.empty() ? 2 : 0;
  }

  // Both of these exit 2 rather than 0. An empty run is a broken invocation, and
  // reporting it as success is how a suite silently stops protecting anything.
  if (all.empty()) {
    std::fprintf(stderr, "No tests are registered.\n");
    return 2;
  }
  if (selected.empty()) {
    std::fprintf(stderr, "Filter '%s' selected none of %zu tests.\n", options.filter.c_str(),
                 all.size());
    return 2;
  }

  std::vector<Report> reports;
  reports.reserve(selected.size());
  const Clock::time_point suite_start = Clock::now();

  for (const testing::TestCase* test : selected) {
    Outcome outcome;
    const Clock::time_point start = Clock::now();
    const unsigned long seh = RunSehGuarded(*test, &outcome);
    const double elapsed_ms =
        std::chrono::duration<double, std::milli>(Clock::now() - start).count();

    if (seh != 0) {
      outcome.passed = false;
      char buffer[64] = {};
      std::snprintf(buffer, sizeof(buffer), "crashed: SEH exception 0x%08lX", seh);
      outcome.message = buffer;
    }

    Report report;
    report.classname = SplitSuite(test->name);
    report.name = test->name.substr(report.classname.size() == test->name.size()
                                        ? 0
                                        : report.classname.size() + 1);
    report.file = RepoRelative(outcome.file.empty() ? test->file : outcome.file);
    report.line = outcome.line != 0 ? outcome.line : test->line;
    report.passed = outcome.passed;
    report.message = outcome.message;
    report.duration_ms = elapsed_ms;

    std::printf("%s %s\n", report.passed ? "PASS" : "FAIL", test->name.c_str());
    if (!report.passed) {
      std::printf("     %s:%d\n", report.file.c_str(), report.line);
      std::printf("     %s\n", report.message.c_str());
    }
    std::fflush(stdout);
    reports.push_back(std::move(report));
  }

  const double total_ms =
      std::chrono::duration<double, std::milli>(Clock::now() - suite_start).count();
  const std::size_t failed = static_cast<std::size_t>(
      std::count_if(reports.begin(), reports.end(), [](const Report& r) { return !r.passed; }));

  std::printf("\n%zu of %zu passed in %.0f ms\n", reports.size() - failed, reports.size(),
              total_ms);

  bool reports_written = true;
  if (!options.junit.empty()) {
    reports_written &= WriteFile(options.junit, BuildJunit(reports, total_ms));
  }
  if (!options.json.empty()) {
    reports_written &= WriteFile(options.json, BuildJson(reports, total_ms));
  }
  // A report that was asked for and could not be written is its own failure: CI
  // would otherwise show a green run with no annotations and nobody would look.
  if (!reports_written) {
    return 2;
  }

  return failed == 0 ? 0 : 1;
}
