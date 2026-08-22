#pragma once

#include <vector>

#include "parent/logic/module_contract.h"

namespace modules {

class ModuleRegistry final {
 public:
  static bool Register(const ModuleDescriptor* descriptor);
  static const std::vector<const ModuleDescriptor*>& All();
  static const ModuleDescriptor* Find(std::wstring_view id);
};

class ModuleRegistrar final {
 public:
  explicit ModuleRegistrar(const ModuleDescriptor* descriptor) {
    registered_ = ModuleRegistry::Register(descriptor);
  }
  bool registered() const { return registered_; }

 private:
  bool registered_ = false;
};

}  // namespace modules
