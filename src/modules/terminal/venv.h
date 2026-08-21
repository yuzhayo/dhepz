#pragma once

#include "modules/terminal/terminal_logic.h"

namespace terminal {

modules::FolderProbeRequest BuildVenvProbe(const LaunchSpec& spec);
bool HasCompatibleVenv(const LaunchSpec& spec,
                       const modules::FolderProbeResult& result);
modules::ProcessRequest BuildVenvCreateRequest(const LaunchSpec& spec,
                                               bool python_fallback = false);

}  // namespace terminal
