#include "platform/launch_request.h"

#include <string_view>

namespace launch {

Request Parse(int argument_count, wchar_t** arguments) {
  Request request;
  if (arguments == nullptr) return request;
  for (int index = 1; index + 1 < argument_count; ++index) {
    const std::wstring_view option(arguments[index]);
    if (option == L"--route") {
      request.route = arguments[++index];
    } else if (option == L"--path") {
      request.working_directory = arguments[++index];
    }
  }
  return request;
}

}  // namespace launch
