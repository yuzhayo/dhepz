#include "modules/terminal/terminal_logic.h"

#include "platform/strings.h"

namespace terminal {

std::wstring BuildCommandLine(const LaunchSpec& spec) {
  std::wstring command;
  switch (spec.shell) {
    case Shell::PowerShell:
      command = L"powershell.exe -NoExit";
      break;
    case Shell::Cmd:
      command = L"cmd.exe";
      break;
    case Shell::Wsl:
      command = L"wsl.exe -d " + str::QuoteArg(spec.wsl_distro);
      break;
  }
  if (spec.venv_enabled && !spec.venv_activate_path.empty()) {
    command += L" -NoExit -Command " + str::QuoteArg(spec.venv_activate_path);
  }
  (void)spec.working_dir;  // applied as the child's working directory at launch
  return command;
}

}  // namespace terminal
