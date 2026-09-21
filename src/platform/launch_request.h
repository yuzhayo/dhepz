#pragma once

#include <string>

namespace launch {

struct Request {
  std::wstring route;
  std::wstring working_directory;
};

Request Parse(int argument_count, wchar_t** arguments);

}  // namespace launch
