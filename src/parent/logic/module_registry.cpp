#include "parent/logic/module_registry.h"

#include <algorithm>

namespace modules {
namespace {

std::vector<const ModuleDescriptor*>& Descriptors() {
  static std::vector<const ModuleDescriptor*> descriptors;
  return descriptors;
}

}  // namespace

bool ModuleRegistry::Register(const ModuleDescriptor* descriptor) {
  if (descriptor == nullptr || descriptor->id.empty() || descriptor->route_id.empty() ||
      descriptor->screen_name.empty() || descriptor->ui_resource_id <= 0 ||
      descriptor->create == nullptr || Find(descriptor->id) != nullptr) {
    return false;
  }
  Descriptors().push_back(descriptor);
  return true;
}

const std::vector<const ModuleDescriptor*>& ModuleRegistry::All() { return Descriptors(); }

const ModuleDescriptor* ModuleRegistry::Find(std::wstring_view id) {
  const auto& descriptors = Descriptors();
  const auto found = std::find_if(descriptors.begin(), descriptors.end(), [id](const auto* item) {
    return item != nullptr && item->id == id;
  });
  return found == descriptors.end() ? nullptr : *found;
}

}  // namespace modules
