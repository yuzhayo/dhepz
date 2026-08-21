// Built-in diagnostics module: always compiled in, cannot be excluded
// (plan Part 3, degraded mode). Records module load/validation failures
// from the gate and surfaces them via Log/ReportStatus; the rendered
// in-app list lands with viewState bindings, the CI artifact with #85.
#include "modules/contract/module_contract.h"
#include "modules/gate/app_gate.h"
#include "modules/registry/module_registry.h"

#include <memory>
#include <string>

namespace {

class DiagnosticsModule final : public modules::ModuleDescriptor {
 public:
  std::wstring_view ModuleId() const override { return L"diagnostics"; }
  std::wstring_view TabLabel() const override { return L"Diagnostics"; }
  int Order() const override { return 900; }
  bool ShowInTabs() const override { return false; }
  std::wstring_view SettingsRoute() const override { return {}; }
  std::vector<std::wstring> DeclaredActions() const override {
    return {L"diagnostics:refresh"};
  }
  std::vector<std::wstring> DeclaredBindings() const override { return {}; }
  std::vector<std::wstring> DeclaredCapabilities() const override { return {}; }

  core::Status Bind(modules::ModuleHost& host) override {
    host_ = &host;
    return core::Ok();
  }

  core::Status Handle(std::wstring_view action, const json::Value&, json::Value*) override {
    if (action != L"diagnostics:refresh" || host_ == nullptr) {
      return core::Err(core::ErrorCode::NotFound, L"unknown action");
    }
    last_summary_.clear();
    for (const modules::RejectEntry& reject : modules::CurrentRejects()) {
      const std::wstring line = reject.module_id + L": " + reject.reason;
      host_->Log(L"warn", line);
      if (!last_summary_.empty()) last_summary_ += L"\n";
      last_summary_ += line;
    }
    return core::Ok();
  }

  void Release() override { host_ = nullptr; }

  static const std::wstring& LastSummary() { return last_summary_; }

 private:
  modules::ModuleHost* host_ = nullptr;
  inline static std::wstring last_summary_;
};

std::unique_ptr<modules::ModuleDescriptor> Make() {
  return std::make_unique<DiagnosticsModule>();
}

const modules::ModuleRegistrar registrar{L"diagnostics", &Make};

}  // namespace

namespace modules {
const std::wstring& DiagnosticsLastSummary() { return DiagnosticsModule::LastSummary(); }
std::unique_ptr<ModuleDescriptor> MakeDiagnosticsForTests() { return Make(); }
}  // namespace modules
