#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "app_version.h"
#include "core/json.h"
#include "modules/gate/app_gate.h"
#include "platform/files.h"
#include "platform/performance_trace.h"
#include "platform/tray_process.h"
#include "ui/config/resolved_ui_document.h"

// Phase 0's tray-resident process: one hidden infrastructure window and one
// tray icon, no UI, no config, no modules. This is the thing whose idle
// cost gets measured (#13), so it stays genuinely minimal: exactly one
// thread, a message loop that blocks in GetMessageW, and nothing that wakes
// when nothing changed.
//
// wWinMain rather than main: SubSystem is Windows, so there is no console
// window to flash on launch. That matters for the ~400 ms cold-start budget
// and for the silent --tray path.
namespace {

// Distinct numeric exit codes for every bootstrap failure, each with a
// MessageBox — no silent exits. The codes start at 10 so they cannot be
// confused with a normal 0/1 result.
[[noreturn]] void BootstrapFailed(unsigned int code, const wchar_t* what) {
  MessageBoxW(nullptr, what, L"dhepz", MB_OK | MB_ICONERROR);
  ExitProcess(code);
}

// Build-time validation (#57): resolve the core catalog plus every screen
// in `screens_dir` and print file(line,column) diagnostics. Console output
// works because CI and the tools invoke us from a shell that owns a console.
int RunValidateUi(const std::wstring& core_path, const std::wstring& screens_dir) {
  std::wstring core_text;
  if (!files::ReadText(core_path, &core_text).ok()) {
    std::wprintf(L"cannot read %ls\n", core_path.c_str());
    return 1;
  }
  json::Value core;
  if (!json::Parse(core_text, &core).ok()) {
    std::wprintf(L"%ls: core catalog does not parse\n", core_path.c_str());
    return 1;
  }

  std::vector<std::wstring> names;
  WIN32_FIND_DATAW entry{};
  const HANDLE find = FindFirstFileW((screens_dir + L"\\*.json").c_str(), &entry);
  if (find != INVALID_HANDLE_VALUE) {
    do {
      names.push_back(entry.cFileName);
    } while (FindNextFileW(find, &entry) != 0);
    FindClose(find);
  }
  std::sort(names.begin(), names.end());

  std::vector<ui::config::ScreenSource> sources;
  for (const std::wstring& name : names) {
    std::wstring text;
    if (!files::ReadText(screens_dir + L"\\" + name, &text).ok()) {
      std::wprintf(L"%ls(1,1): cannot read screen file\n", name.c_str());
      return 1;
    }
    sources.push_back({name, std::move(text)});
  }

  std::vector<ui::config::Diagnostic> diagnostics;
  std::unique_ptr<ui::config::ResolvedUiDocument> document;
  const core::Status status =
      ui::config::ResolveDocument(core, sources, &diagnostics, &document);
  for (const ui::config::Diagnostic& diagnostic : diagnostics) {
    std::wprintf(L"%ls(%d,%d): %ls\n", screens_dir.c_str(), diagnostic.line, diagnostic.column,
                 diagnostic.message.c_str());
  }
  if (!status.ok()) {
    std::wprintf(L"UI config invalid: %zu diagnostic(s)\n", diagnostics.size());
    return 1;
  }
  std::wprintf(L"UI config ok: %zu route(s)\n", document->routes().size());
  return 0;
}

int RunValidateEmbedded() {
  modules::AppGate gate;
  const core::Status started = gate.Start();
  if (!started.ok()) {
    std::wprintf(L"embedded UI startup failed: %ls\n", started.Message().c_str());
    return 1;
  }
  for (const modules::RejectEntry& reject : gate.Rejects()) {
    std::wprintf(L"%ls(%d,%d): module %ls: %ls\n", reject.file.c_str(),
                 reject.line, reject.column, reject.module_id.c_str(),
                 reject.reason.c_str());
  }
  if (!gate.Rejects().empty()) {
    std::wprintf(L"embedded module contract invalid: %zu rejection(s)\n",
                 gate.Rejects().size());
    return 1;
  }
  if (!gate.Mounted(L"terminal") || !gate.Mounted(L"diagnostics")) {
    std::wprintf(L"embedded UI omitted terminal or diagnostics\n");
    return 1;
  }
  std::wprintf(L"embedded UI ok: %zu module(s), %zu route(s)\n",
               gate.Peers().size(), gate.document()->routes().size());
  return 0;
}

}  // namespace

int APIENTRY wWinMain(_In_ HINSTANCE instance,
                      _In_opt_ HINSTANCE previous,
                      _In_ LPWSTR command_line,
                      _In_ int show_command) {
  UNREFERENCED_PARAMETER(previous);
  UNREFERENCED_PARAMETER(command_line);
  UNREFERENCED_PARAMETER(show_command);

  static_assert(dhepz::version::kMajor >= 0,
                "version.props must reach the compiler as defines");

  // Tooling path (#57): validate the UI config and exit, no tray, no window.
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  for (int i = 0; i < argc && argv != nullptr; ++i) {
    if (lstrcmpW(argv[i], L"--validate-embedded") == 0) {
      const int code = RunValidateEmbedded();
      LocalFree(argv);
      return code;
    }
  }
  for (int i = 0; i + 2 < argc + 1 && argv != nullptr; ++i) {
    if (lstrcmpW(argv[i], L"--validate-ui") == 0 && i + 2 <= argc - 1) {
      const int code = RunValidateUi(argv[i + 1], argv[i + 2]);
      LocalFree(argv);
      return code;
    }
  }
  if (argv != nullptr) {
    LocalFree(argv);
  }

  // The QPC captured on the first line IS the ProcessEntry milestone: the
  // session emits it as soon as the provider registers. The tray-only path
  // has no window by design, so the window milestones (ConfigResolved …
  // FirstFrameVisible) never fire here; they join with the code that shows
  // a window.
  const std::int64_t process_entry_qpc = trace::CurrentQpc();
  trace::PerformanceTraceSession session(process_entry_qpc);

  tray::TrayProcess tray_process;
  switch (tray_process.Start(instance)) {
    case tray::StartResult::Ok:
      break;
    case tray::StartResult::WindowClassFailed:
      BootstrapFailed(10, L"The infrastructure window class could not be registered.");
    case tray::StartResult::WindowCreateFailed:
      BootstrapFailed(11, L"The infrastructure window could not be created.");
    case tray::StartResult::TaskbarMessageFailed:
      BootstrapFailed(12, L"The TaskbarCreated message could not be registered.");
  }

  // Non-fatal by design: a headless session has no shell to host the icon,
  // and the process must still run (CI, automated runs).
  tray_process.InstallTray();

  const int result = tray_process.Run();
  tray_process.Shutdown();
  return result;
}
