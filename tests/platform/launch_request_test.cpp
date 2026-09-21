#include "platform/launch_request.h"

#include "framework/test_case.h"

DHEPZ_TEST(LaunchRequest, ReadsRouteAndExplorerPath) {
  wchar_t program[] = L"dhepz.exe";
  wchar_t route_option[] = L"--route";
  wchar_t route[] = L"terminal";
  wchar_t path_option[] = L"--path";
  wchar_t path[] = L"C:\\folder with spaces";
  wchar_t* arguments[]{program, route_option, route, path_option, path};

  const launch::Request request = launch::Parse(5, arguments);
  DHEPZ_CHECK_EQ(request.route, std::wstring(L"terminal"));
  DHEPZ_CHECK_EQ(request.working_directory,
                 std::wstring(L"C:\\folder with spaces"));
}
