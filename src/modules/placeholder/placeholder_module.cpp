#include "parent/logic/module_registry.h"

#include <memory>

#include "modules/placeholder/placeholder_resources.h"

// Intentionally empty feature module. It exists to confirm the G5 drop-in
// contract end to end: one folder under src/modules/ adds a tab, and nothing
// outside this folder is edited. The screen has no children and the
// controller binds no actions, so the module owns no logic and no state.
namespace modules::placeholder {
namespace {

class PlaceholderController final : public ModuleController {
 public:
  ui::application::UiPatch InitialState(const ModuleHost& host) const override {
    (void)host;
    return {};
  }

  core::Status Bind(ModuleHost* host,
                    ui::application::UiActionRegistry* actions) override {
    if (host == nullptr || actions == nullptr) {
      return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                       L"placeholder module requires its parent contract");
    }
    return core::Ok();
  }
};

std::unique_ptr<ModuleController> CreatePlaceholderController() {
  return std::make_unique<PlaceholderController>();
}

const ModuleDescriptor kPlaceholderDescriptor{
    L"placeholder", L"placeholder", L"modules/placeholder/placeholder.json",
    IDR_PLACEHOLDER_SCREEN_JSON, &CreatePlaceholderController};
const ModuleRegistrar kPlaceholderRegistration(&kPlaceholderDescriptor);

}  // namespace
}  // namespace modules::placeholder
