#include "modules/terminal/wsl.h"

#include <string>

#include "framework/test_case.h"

namespace {

const std::wstring kTwoDistros =
    L"Windows Subsystem for Linux Distributions:\nUbuntu (Default)\r\nDebian\r\n";
const std::wstring kThreeDistros =
    L"Windows Subsystem for Linux Distributions:\nUbuntu (Default)\r\nDebian\r\nAlpine\r\n";

}  // namespace

DHEPZ_TEST(Wsl, ParseSkipsHeaderBlankLinesAndDefaultMarker) {
  const std::vector<std::wstring> distros =
      terminal::WslEnumerator::ParseListOutput(kTwoDistros);
  DHEPZ_CHECK_EQ(distros.size(), static_cast<std::size_t>(2));
  DHEPZ_CHECK_EQ(distros[0], std::wstring(L"Ubuntu"));
  DHEPZ_CHECK_EQ(distros[1], std::wstring(L"Debian"));
}

DHEPZ_TEST(Wsl, RefreshCachesPerSession) {
  terminal::WslEnumerator enumerator(
      [](std::wstring_view, process::RunResult* out) {
        out->output = kTwoDistros;
        return core::Ok();
      });
  DHEPZ_CHECK(!enumerator.cached());
  DHEPZ_CHECK(enumerator.Refresh().ok());
  DHEPZ_CHECK(enumerator.cached());
  DHEPZ_CHECK_EQ(enumerator.Distros().size(), static_cast<std::size_t>(2));
}

DHEPZ_TEST(Wsl, FailedRefreshKeepsThePreviousCache) {
  int calls = 0;
  terminal::WslEnumerator enumerator([&calls](std::wstring_view, process::RunResult* out) {
    if (++calls == 1) {
      out->output = kTwoDistros;
      return core::Ok();
    }
    return core::Err(core::ErrorCode::IoError, L"wsl missing");
  });
  DHEPZ_CHECK(enumerator.Refresh().ok());
  DHEPZ_CHECK(!enumerator.Refresh().ok());
  DHEPZ_CHECK_EQ(enumerator.Distros().size(), static_cast<std::size_t>(2));
}

DHEPZ_TEST(Wsl, ExplicitRefreshPicksUpNewDistros) {
  int calls = 0;
  terminal::WslEnumerator enumerator([&calls](std::wstring_view, process::RunResult* out) {
    out->output = (++calls == 1) ? kTwoDistros : kThreeDistros;
    return core::Ok();
  });
  DHEPZ_CHECK(enumerator.Refresh().ok());
  DHEPZ_CHECK_EQ(enumerator.Distros().size(), static_cast<std::size_t>(2));
  DHEPZ_CHECK(enumerator.Refresh().ok());
  DHEPZ_CHECK_EQ(enumerator.Distros().size(), static_cast<std::size_t>(3));
}
