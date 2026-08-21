#include "modules/registry/module_registry.h"

#include <memory>
#include <string>

#include "framework/test_case.h"

namespace {

class StubDescriptor final : public modules::ModuleDescriptor {
 public:
  explicit StubDescriptor(std::wstring id) : id_(std::move(id)) {}
  std::wstring_view ModuleId() const override { return id_; }
  std::wstring_view TabLabel() const override { return id_; }
  int Order() const override { return 100; }
  bool ShowInTabs() const override { return true; }
  std::wstring_view SettingsRoute() const override { return {}; }
  std::vector<std::wstring> DeclaredActions() const override { return {}; }
  std::vector<std::wstring> DeclaredBindings() const override { return {}; }
  std::vector<std::wstring> DeclaredCapabilities() const override { return {}; }
  core::Status Bind(modules::ModuleHost&) override { return core::Ok(); }
  core::Status Handle(std::wstring_view, const json::Value&, json::Value*) override {
    return core::Ok();
  }
  void Release() override {}

 private:
  std::wstring id_;
};

std::unique_ptr<modules::ModuleDescriptor> MakeAlpha() {
  return std::make_unique<StubDescriptor>(L"alpha");
}
std::unique_ptr<modules::ModuleDescriptor> MakeBeta() {
  return std::make_unique<StubDescriptor>(L"beta");
}

// The static self-registration pattern modules use: runs before main.
bool g_ran_before_main = false;
struct SelfTestRegistrar {
  SelfTestRegistrar() {
    modules::RegisterModule(L"static-selftest", &MakeAlpha);
    g_ran_before_main = true;
  }
};
const SelfTestRegistrar kSelfTest{};

}  // namespace

DHEPZ_TEST(ModuleRegistry, StaticRegistrarRunsBeforeMain) {
  DHEPZ_CHECK(g_ran_before_main);
  modules::ResetRegistryForTests();
}

DHEPZ_TEST(ModuleRegistry, DuplicateModuleIdFirstWinsAndIsReported) {
  modules::ResetRegistryForTests();
  modules::RegisterModule(L"alpha", &MakeAlpha);
  modules::RegisterModule(L"alpha", &MakeBeta);
  modules::RegisterModule(L"beta", &MakeBeta);

  const std::vector<modules::RegisteredModule> modules = modules::CollectModules();
  DHEPZ_CHECK_EQ(modules.size(), static_cast<std::size_t>(2));
  DHEPZ_CHECK_EQ(modules[0].module_id, std::wstring(L"alpha"));
  DHEPZ_CHECK(modules[0].factory == &MakeAlpha);  // first registration wins
  DHEPZ_CHECK_EQ(modules[1].module_id, std::wstring(L"beta"));

  DHEPZ_CHECK_EQ(modules::RegistryDiagnostics().size(), static_cast<std::size_t>(1));
  DHEPZ_CHECK(modules::RegistryDiagnostics()[0].message.find(L"alpha") !=
              std::wstring::npos);

  // Factories produce live descriptors through the contract only.
  std::unique_ptr<modules::ModuleDescriptor> live = modules[0].factory();
  DHEPZ_CHECK(live != nullptr);
  DHEPZ_CHECK_EQ(live->ModuleId(), std::wstring_view(L"alpha"));
  modules::ResetRegistryForTests();
}
