#pragma once

#include <string_view>
#include <vector>

#include "ui/components/component.h"

namespace ui::components {

class ComponentRegistry final {
 public:
  ComponentRegistry();
  const ComponentDescriptor* Find(std::wstring_view type) const;

 private:
  std::vector<ComponentDescriptor> components_;
};

}  // namespace ui::components
