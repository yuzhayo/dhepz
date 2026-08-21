#include "modules/registry/module_registry.h"
#include "modules/terminal/terminal_module.h"

namespace terminal {
namespace {

const modules::ModuleRegistrar registrar{L"terminal", &MakeTerminalForTests};

}  // namespace
}  // namespace terminal
