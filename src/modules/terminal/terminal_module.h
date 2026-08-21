// Terminal module scaffold (P4-01): self-contained folder, discovered
// without central edits, manifest valid with no capabilities.
#pragma once

#include <memory>

#include "modules/contract/module_contract.h"

namespace terminal {

std::unique_ptr<modules::ModuleDescriptor> MakeTerminalForTests();

}  // namespace terminal
