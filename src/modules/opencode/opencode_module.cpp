#include "parent/logic/module_registry.h"

#include <memory>

#include "modules/opencode/opencode_resources.h"

// Intentionally empty feature module. It exists as a drop-in placeholder tab:
// one folder under src/modules/ adds the tab, and nothing outside this folder
// is edited. The screen has no children and the controller binds no actions,
// so the module owns no logic and no state yet.
namespace modules::opencode {
namespace {

class OpencodeController final : public ModuleController {
 public:
  ui::application::UiPatch InitialState(const ModuleHost& host) const override {
    (void)host;
    return {};
  }

  core::Status Bind(ModuleHost* host,
                    ui::application::UiActionRegistry* actions) override {
    if (host == nullptr || actions == nullptr) {
      return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                       L"opencode module requires its parent contract");
    }
    return core::Ok();
  }
};

std::unique_ptr<ModuleController> CreateOpencodeController() {
  return std::make_unique<OpencodeController>();
}

const ModuleDescriptor kOpencodeDescriptor{
    L"opencode", L"opencode", L"modules/opencode/opencode.json",
    IDR_OPENCODE_SCREEN_JSON, &CreateOpencodeController};
const ModuleRegistrar kOpencodeRegistration(&kOpencodeDescriptor);

}  // namespace
}  // namespace modules::opencode
