#include <windows.h>

#include "app_version.h"

// Phase 0 skeleton. This exists so the flag set in dhepz.vcxproj can be
// verified by a real build and link (issue #1). The tray-resident process
// lands in issue #11.
//
// wWinMain rather than main: SubSystem is Windows, so there is no console
// window to flash on launch. That matters for the ~400 ms cold-start budget
// and for the silent --tray path.
int APIENTRY wWinMain(_In_ HINSTANCE instance,
                      _In_opt_ HINSTANCE previous,
                      _In_ LPWSTR command_line,
                      _In_ int show_command) {
  UNREFERENCED_PARAMETER(instance);
  UNREFERENCED_PARAMETER(previous);
  UNREFERENCED_PARAMETER(command_line);
  UNREFERENCED_PARAMETER(show_command);

  static_assert(dhepz::version::kMajor >= 0,
                "version.props must reach the compiler as defines");

  return 0;
}
