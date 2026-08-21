#include "modules/registry/module_registry.h"

#include <mutex>

namespace modules {
namespace {

struct RegistryState {
  std::mutex mutex;
  std::vector<RegisteredModule> modules;
  std::vector<RegistryDiagnostic> diagnostics;
};

RegistryState& State() {
  static RegistryState state;
  return state;
}

}  // namespace

void RegisterModule(std::wstring_view module_id, ModuleFactory factory) {
  RegistryState& state = State();
  std::lock_guard lock(state.mutex);
  for (const RegisteredModule& existing : state.modules) {
    if (existing.module_id == module_id) {
      state.diagnostics.push_back(
          {L"duplicate moduleId '" + std::wstring(module_id) +
           L"': first registration wins, later one skipped"});
      return;
    }
  }
  state.modules.push_back({std::wstring(module_id), factory});
}

std::vector<RegisteredModule> CollectModules() {
  RegistryState& state = State();
  std::lock_guard lock(state.mutex);
  return state.modules;
}

const std::vector<RegistryDiagnostic>& RegistryDiagnostics() {
  return State().diagnostics;
}

void ResetRegistryForTests() {
  RegistryState& state = State();
  std::lock_guard lock(state.mutex);
  state.modules.clear();
  state.diagnostics.clear();
}

}  // namespace modules
