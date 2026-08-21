// ModuleRegistry — self-registration and discovery (P3-02).
//
// Modules compile directly into the EXE and register from static
// initializers; the registry is an in-memory list, no I/O at startup (G2).
// A static .lib would let the linker drop the unreferenced registration
// object, so modules are always whole-EXE (AGENTS.md).
//
// Discovery order is registration order, which is deterministic for a
// given binary. Duplicate moduleIds: first registration wins, the conflict
// is recorded for diagnostics, never a crash.
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "modules/contract/module_contract.h"

namespace modules {

using ModuleFactory = std::unique_ptr<ModuleDescriptor> (*)();

struct RegisteredModule {
  std::wstring module_id;
  ModuleFactory factory;
};

struct RegistryDiagnostic {
  std::wstring message;
};

// Called from a module's static initializer (via ModuleRegistrar).
void RegisterModule(std::wstring_view module_id, ModuleFactory factory);

// Enumerates self-registered providers; in-memory, no I/O.
std::vector<RegisteredModule> CollectModules();
const std::vector<RegistryDiagnostic>& RegistryDiagnostics();

// Tests only: clears registrations and diagnostics.
void ResetRegistryForTests();

// Static self-registration helper: a file-scope instance registers the
// module before main runs.
struct ModuleRegistrar {
  ModuleRegistrar(std::wstring_view module_id, ModuleFactory factory) {
    RegisterModule(module_id, factory);
  }
};

}  // namespace modules
