#include "platform/process.h"

#include <string>

#include "framework/test_case.h"

DHEPZ_TEST(Process, RunCaptureCapturesOutputAndExitCode) {
  process::RunResult result;
  DHEPZ_CHECK(
      process::RunCapture(L"cmd.exe /c echo dhepz-proc-test", L"", 10000, &result).ok());
  DHEPZ_CHECK(!result.timed_out);
  DHEPZ_CHECK_EQ(result.exit_code, 0);
  DHEPZ_CHECK(result.output.find(L"dhepz-proc-test") != std::wstring::npos);
}

DHEPZ_TEST(Process, RunCaptureReportsExitCode) {
  process::RunResult result;
  DHEPZ_CHECK(process::RunCapture(L"cmd.exe /c exit 7", L"", 10000, &result).ok());
  DHEPZ_CHECK(!result.timed_out);
  DHEPZ_CHECK_EQ(result.exit_code, 7);
}

DHEPZ_TEST(Process, RunCaptureHonoursTheDeadline) {
  process::RunResult result;
  DHEPZ_CHECK(process::RunCapture(L"powershell.exe -NoProfile -Command Start-Sleep 5",
                                  L"", 400, &result)
                  .ok());
  DHEPZ_CHECK(result.timed_out);
}

DHEPZ_TEST(Process, LaunchSmoke) {
  DHEPZ_CHECK(process::Launch(L"", L"cmd.exe /c exit 0", L"", process::WindowMode::Hidden)
                  .ok());
}

DHEPZ_TEST(Process, ShellLaunchMissingFileFailsWithStatus) {
  const core::Status status =
      process::ShellLaunch(L"open", L"C:\\dhepz-does-not-exist-9x7q.exe", L"", L"");
  DHEPZ_CHECK(!status.ok());
}
