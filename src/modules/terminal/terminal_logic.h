// Terminal feature logic — no UI types (plan: terminal_logic.* is the
// feature itself). Command lines are built strictly through QuoteArg.
#pragma once

#include <string>

namespace terminal {

enum class Shell { PowerShell, Cmd, Wsl };

struct LaunchSpec {
  Shell shell = Shell::PowerShell;
  std::wstring working_dir;
  std::wstring wsl_distro;   // used when shell == Wsl
  bool admin = false;       // elevation handled by ShellLaunch (P4-02)
  bool venv_enabled = false;
  std::wstring venv_activate_path;
};

// Builds the child command line; every argument quoted via str::QuoteArg.
std::wstring BuildCommandLine(const LaunchSpec& spec);

}  // namespace terminal
