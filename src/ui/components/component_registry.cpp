#include "ui/components/component_registry.h"

#include "ui/components/button/button_component.h"
#include "ui/components/card/card_component.h"
#include "ui/components/checkbox/checkbox_component.h"
#include "ui/components/combo/combo_component.h"
#include "ui/components/container/container_component.h"
#include "ui/components/dialog/dialog_component.h"
#include "ui/components/input/input_component.h"
#include "ui/components/list/list_component.h"
#include "ui/components/scrollbar/scrollbar_component.h"
#include "ui/components/screen/screen_component.h"
#include "ui/components/tabs/tabs_component.h"
#include "ui/components/text/text_component.h"
#include "ui/components/toggle/toggle_component.h"
#include "ui/components/window/window_component.h"

namespace ui::components {

ComponentRegistry::ComponentRegistry()
    : components_{CreateWindowComponent(), CreateScreenComponent(), CreateContainerComponent(),
                  CreateTextComponent(), CreateButtonComponent(), CreateInputComponent(),
                  CreateComboComponent(), CreateCheckboxComponent(), CreateToggleComponent(),
                  CreateCardComponent(), CreateListComponent(), CreateScrollbarComponent(),
                  CreateDialogComponent(), CreateTabsComponent()} {}

const ComponentDescriptor* ComponentRegistry::Find(std::wstring_view type) const {
  for (const ComponentDescriptor& component : components_) {
    if (component.type == type) return &component;
  }
  return nullptr;
}

}  // namespace ui::components
