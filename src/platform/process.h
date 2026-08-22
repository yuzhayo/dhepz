#pragma once

#include <string>

#include "core/status.h"
#include "parent/logic/module_contract.h"

namespace process {

core::Status Start(const modules::ProcessRequest& request);
core::Status Run(const modules::ProcessRequest& request, std::wstring* standard_output);

}  // namespace process
