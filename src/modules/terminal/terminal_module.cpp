// Self-registering logic half of the terminal module (P4-01). The real
// launch path lands in P4-02; until then terminal:launch reports
// Unsupported so a missing implementation can never masquerade as success.
#include "modules/contract/module_contract.h"
#include "modules/registry/module_registry.h"
#include "modules/terminal/terminal_logic.h"

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
    // P4-02 wires ProcessRun; until then refuse loudly-but-gracefully.
    return core::Err(core::ErrorCode::Unsupported,
                     L"terminal:launch lands with P4-02 process launch");
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
