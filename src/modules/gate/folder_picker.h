#pragma once

#include "modules/contract/module_contract.h"

namespace modules {

core::Status ShowNativeFolderPicker(void* owner_window,
                                    const FolderPickerRequest& request,
                                    FolderPickerResult* result);

}  // namespace modules
