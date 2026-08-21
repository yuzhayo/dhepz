#include "modules/terminal/wsl.h"

#include <string>

#include "framework/test_case.h"

namespace {

const std::wstring kTwoDistros =
    L"Ubuntu\r\nDebian\r\n";
const std::wstring kThreeDistros =
    L"Ubuntu\r\nDebian\r\nAlpine\r\n";

}  // namespace

DHEPZ_TEST(Wsl, HeaderlessOutputKeepsFirstDistroAndFinalUnterminatedLine) {
  std::wstring output = L"\uFEFF";
  output.push_back(L'\0');
  output.append(L"Ubuntu");
  output.push_back(L'\0');
  output.append(L"\r\n\r\n  Debian  ");
  const std::vector<std::wstring> distros =
      terminal::ParseWslListOutput(output);
  DHEPZ_CHECK_EQ(distros.size(), static_cast<std::size_t>(2));
  DHEPZ_CHECK_EQ(distros[0], std::wstring(L"Ubuntu"));
  DHEPZ_CHECK_EQ(distros[1], std::wstring(L"Debian"));
}

DHEPZ_TEST(Wsl, RecognizedHeaderAndDefaultSuffixAreCompatibilityOnly) {
  const std::vector<std::wstring> distros = terminal::ParseWslListOutput(
      L"Windows Subsystem for Linux Distributions:\r\nUbuntu (Default)\r\n"
      L"Windows Subsystem distro\r\nLiteral(Default)\r\n");
  DHEPZ_CHECK_EQ(distros.size(), static_cast<std::size_t>(3));
  DHEPZ_CHECK_EQ(distros[0], std::wstring(L"Ubuntu"));
  DHEPZ_CHECK_EQ(distros[1], std::wstring(L"Windows Subsystem distro"));
  DHEPZ_CHECK_EQ(distros[2], std::wstring(L"Literal(Default)"));
}

DHEPZ_TEST(Wsl, OrderingIsStableAcrossDecodedFixtures) {
  const std::vector<std::wstring> two = terminal::ParseWslListOutput(kTwoDistros);
  const std::vector<std::wstring> three = terminal::ParseWslListOutput(kThreeDistros);
  DHEPZ_CHECK_EQ(two.size(), static_cast<std::size_t>(2));
  DHEPZ_CHECK_EQ(three.size(), static_cast<std::size_t>(3));
  DHEPZ_CHECK_EQ(three[0], std::wstring(L"Ubuntu"));
  DHEPZ_CHECK_EQ(three[1], std::wstring(L"Debian"));
  DHEPZ_CHECK_EQ(three[2], std::wstring(L"Alpine"));
}
