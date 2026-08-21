// Self-registering logic half of the terminal module. terminal:launch opens
// an external console (P4-02); RunCapture-based flows (WSL enumeration etc.)
// land with worker offload (P4-05).
#include "modules/contract/module_contract.h"
#include "modules/registry/module_registry.h"
#include "modules/terminal/terminal_logic.h"
#include "platform/process.h"

#include <memory>

namespace terminal {
namespace {

class TerminalModule final : public modules::ModuleDescriptor {
 public:
  std::wstring_view ModuleId() const override { return L"terminal"; }
  std::wstring_view TabLabel() const override { return L"Terminal"; }
  int Order() const override { return 10; }
  bool ShowInTabs() const override { return true; }
  std::wstring_view SettingsRoute() const override { return {}; }
  std::vector<std::wstring> DeclaredActions() const override { return {L"terminal:launch"}; }
  std::vector<std::wstring> DeclaredBindings() const override { return {}; }
  std::vector<std::wstring> DeclaredCapabilities() const override { return {}; }

  core::Status Bind(modules::ModuleHost& host) override {
    host_ = &host;
    return core::Ok();
  }

  core::Status Handle(std::wstring_view action, const json::Value&, json::Value*) override {
    if (action != L"terminal:launch") {
      return core::Err(core::ErrorCode::NotFound, L"terminal: unknown action");
    }
    LaunchSpec spec;
    const std::wstring command = BuildCommandLine(spec);
    return process::Launch(L"", command, spec.working_dir, process::WindowMode::NewConsole);
  }

  void Release() override { host_ = nullptr; }

 private:
  modules::ModuleHost* host_ = nullptr;
};

std::unique_ptr<modules::ModuleDescriptor> Make() {
  return std::make_unique<TerminalModule>();
}

const modules::ModuleRegistrar registrar{L"terminal", &Make};

}  // namespace

std::unique_ptr<modules::ModuleDescriptor> MakeTerminalForTests() { return Make(); }

}  // namespace terminal
