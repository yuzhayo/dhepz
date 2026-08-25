#include <windows.h>
#include <shellapi.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "app_version.h"
#include "orchestrator/window_orchestrator.h"
#include "parent/logic/module_registry.h"
#include "platform/performance_trace.h"
#include "platform/app_update_service.h"
#include "platform/tray_process.h"
#include "parent/ui/config/embedded_settings_loader.h"

// P3 composition root: one tray-resident owner process, one or more empty main
// AppWindows, and one core Settings companion per main window. Settings uses
// the generic parent UI but is not a feature module. The single UI thread
// blocks in GetMessageW while idle.
//
// wWinMain rather than main: SubSystem is Windows, so there is no console
// window to flash on launch. That matters for the ~400 ms cold-start budget
// and for resident tray activation.
namespace {

// Distinct numeric exit codes for every bootstrap failure, each with a
// MessageBox — no silent exits. The codes start at 10 so they cannot be
// confused with a normal 0/1 result.
[[noreturn]] void BootstrapFailed(unsigned int code, const wchar_t* what) {
  MessageBoxW(nullptr, what, L"dhepz", MB_OK | MB_ICONERROR);
  ExitProcess(code);
}

std::wstring RequestedRoute() {
  int count = 0;
  wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
  if (arguments == nullptr) return {};
  std::wstring route;
  for (int index = 1; index + 1 < count; ++index) {
    if (std::wstring_view(arguments[index]) == L"--route") {
      route = arguments[index + 1];
      break;
    }
  }
  LocalFree(arguments);
  return route;
}

}  // namespace

int APIENTRY wWinMain(_In_ HINSTANCE instance,
                      _In_opt_ HINSTANCE previous,
                      _In_ LPWSTR command_line,
                      _In_ int show_command) {
  UNREFERENCED_PARAMETER(previous);
  UNREFERENCED_PARAMETER(command_line);
  UNREFERENCED_PARAMETER(show_command);

  update::RunVelopackStartup();

  static_assert(dhepz::version::kMajor >= 0,
                "version.props must reach the compiler as defines");

  // The QPC captured on the first line is the ProcessEntry milestone. Later
  // phases add config-resolution milestones; P2 goes directly from bootstrap
  // to creating and showing an empty chrome container.
  const std::int64_t process_entry_qpc = trace::CurrentQpc();
  trace::PerformanceTraceSession session(process_entry_qpc);
  const std::wstring requested_route = RequestedRoute();

  tray::TrayProcess tray_process;
  switch (tray_process.Start(instance, requested_route)) {
    case tray::StartResult::Ok:
      break;
    case tray::StartResult::ExistingOwnerNotified:
      return 0;
    case tray::StartResult::SingleInstanceFailed:
      BootstrapFailed(10, L"The single-instance owner could not be established.");
    case tray::StartResult::ExistingOwnerNotificationFailed:
      BootstrapFailed(11, L"The existing application process could not create a window.");
    case tray::StartResult::WindowClassFailed:
      BootstrapFailed(12, L"The infrastructure window class could not be registered.");
    case tray::StartResult::WindowCreateFailed:
      BootstrapFailed(13, L"The infrastructure window could not be created.");
    case tray::StartResult::TaskbarMessageFailed:
      BootstrapFailed(14, L"The TaskbarCreated message could not be registered.");
  }

  // Only the resident owner resolves UI. Secondary launches notify that
  // owner first and exit immediately, keeping taskbar activation responsive.
  // Settings is required core UI: a missing or contract-invalid resource is
  // a startup failure rather than a silently degraded window.
  std::vector<ui::config::Diagnostic> settings_diagnostics;
  std::unique_ptr<ui::config::ResolvedUiDocument> settings_document;
  const core::Status settings_status = ui::config::LoadEmbeddedSettingsDocument(
      instance, &settings_diagnostics, &settings_document);
  if (!settings_status.ok()) {
    tray_process.Shutdown();
    const std::wstring message = L"The required Settings UI could not be loaded.\n\n" +
                                 settings_status.Message();
    BootstrapFailed(15, message.c_str());
  }

  const auto& module_descriptors = modules::ModuleRegistry::All();
  if (module_descriptors.empty()) {
    tray_process.Shutdown();
    BootstrapFailed(16, L"No feature module is registered.");
  }
  std::vector<ui::config::EmbeddedScreenResource> module_resources;
  module_resources.reserve(module_descriptors.size());
  for (const modules::ModuleDescriptor* descriptor : module_descriptors) {
    module_resources.push_back(
        {std::wstring(descriptor->screen_name), descriptor->ui_resource_id});
  }
  std::vector<ui::config::Diagnostic> feature_diagnostics;
  std::unique_ptr<ui::config::ResolvedUiDocument> feature_document;
  const core::Status feature_status = ui::config::LoadEmbeddedFeatureDocument(
      instance, module_resources, &feature_diagnostics, &feature_document);
  if (!feature_status.ok()) {
    tray_process.Shutdown();
    const std::wstring message = L"The feature UI could not be loaded.\n\n" +
                                 feature_status.Message();
    BootstrapFailed(17, message.c_str());
  }
  for (const modules::ModuleDescriptor* descriptor : module_descriptors) {
    if (feature_document->FindRoute(descriptor->route_id) == nullptr) {
      tray_process.Shutdown();
      BootstrapFailed(18, L"A module route is missing from its UI document.");
    }
  }

  // Non-fatal by design: a headless session has no shell to host the icon,
  // and the process must still run (CI, automated runs).
  tray_process.InstallTray();

  // One resident owner PID hosts every window. A secondary EXE invocation
  // asks the orchestrator for another window, then exits without a tray of
  // its own.
  orchestrator::WindowOrchestrator windows(instance, settings_document.get(),
                                            feature_document.get(),
                                            module_descriptors);

  tray_process.set_launch_handler([&](std::wstring route) {
    if (!windows.OpenWindow(route)) {
      MessageBoxW(nullptr, L"The application window could not be created.", L"dhepz",
                  MB_OK | MB_ICONERROR);
    }
  });
  if (!windows.OpenWindow(requested_route)) {
    tray_process.Shutdown();
    BootstrapFailed(19, L"The application window could not be created.");
  }

  const int result = tray_process.Run();
  windows.CloseAll();
  tray_process.Shutdown();
  return result;
}
