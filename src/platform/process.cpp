#include "platform/process.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <string>
#include <vector>

#include "platform/strings.h"

namespace process {
namespace {

std::wstring ErrorText(DWORD code) {
  wchar_t* raw = nullptr;
  const DWORD size = FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, code, 0, reinterpret_cast<wchar_t*>(&raw), 0, nullptr);
  std::wstring text = size != 0 && raw != nullptr ? std::wstring(raw, size) : L"error " + std::to_wstring(code);
  if (raw != nullptr) LocalFree(raw);
  while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' ')) {
    text.pop_back();
  }
  return text;
}

std::wstring Arguments(const std::vector<std::wstring>& arguments) {
  std::wstring command;
  for (const std::wstring& argument : arguments) {
    if (!command.empty()) command.push_back(L' ');
    command.append(str::QuoteArg(argument));
  }
  return command;
}

core::Status ResolveExecutable(std::wstring_view requested, std::wstring* resolved) {
  if (resolved == nullptr || requested.empty()) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument, L"Process executable is required");
  }
  const std::wstring name(requested);
  if (name.find_first_of(L"\\/") != std::wstring::npos) {
    const DWORD attributes = GetFileAttributesW(name.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
      return DHEPZ_ERR(core::ErrorCode::NotFound, L"Executable was not found: " + name);
    }
    *resolved = name;
    return core::Ok();
  }
  std::vector<wchar_t> buffer(MAX_PATH);
  for (;;) {
    const DWORD length = SearchPathW(nullptr, name.c_str(), nullptr,
                                     static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
    if (length == 0) {
      return DHEPZ_ERR(core::ErrorCode::NotFound, L"Executable was not found: " + name);
    }
    if (length < buffer.size()) {
      resolved->assign(buffer.data(), length);
      return core::Ok();
    }
    buffer.resize(static_cast<std::size_t>(length) + 1);
  }
}

std::wstring CommandLine(const modules::ProcessRequest& request,
                         std::wstring_view executable) {
  std::wstring command = L"\"" + std::wstring(executable) + L"\"";
  const std::wstring arguments = Arguments(request.arguments);
  if (!arguments.empty()) command.append(L" ").append(arguments);
  return command;
}

core::Status StartCreateProcess(const modules::ProcessRequest& request, HANDLE output) {
  std::wstring executable;
  DHEPZ_RETURN_IF_ERROR(ResolveExecutable(request.executable, &executable));
  std::wstring command = CommandLine(request, executable);
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  if (output != nullptr) {
    startup.dwFlags |= STARTF_USESTDHANDLES;
    startup.hStdOutput = output;
    startup.hStdError = output;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  }
  PROCESS_INFORMATION info{};
  const DWORD flags = request.hidden ? CREATE_NO_WINDOW : 0;
  const BOOL started = CreateProcessW(
      executable.c_str(), command.data(), nullptr, nullptr, output != nullptr, flags,
      nullptr, request.working_directory.empty() ? nullptr : request.working_directory.c_str(),
      &startup, &info);
  if (!started) {
    return DHEPZ_ERR(core::ErrorCode::IoError,
                     L"Cannot start " + request.executable + L": " + ErrorText(GetLastError()));
  }
  CloseHandle(info.hThread);
  CloseHandle(info.hProcess);
  return core::Ok();
}

std::wstring DecodeOutput(const std::string& bytes) {
  if (bytes.empty()) return {};
  const bool utf16 = bytes.size() >= 2 &&
                     ((static_cast<unsigned char>(bytes[0]) == 0xFF &&
                       static_cast<unsigned char>(bytes[1]) == 0xFE) ||
                      (bytes.size() >= 4 && bytes[1] == '\0' && bytes[3] == '\0'));
  if (utf16) {
    const std::size_t offset = static_cast<unsigned char>(bytes[0]) == 0xFF ? 2 : 0;
    std::wstring text;
    text.reserve((bytes.size() - offset) / 2);
    for (std::size_t index = offset; index + 1 < bytes.size(); index += 2) {
      const unsigned int value = static_cast<unsigned char>(bytes[index]) |
                                 (static_cast<unsigned int>(static_cast<unsigned char>(bytes[index + 1])) << 8);
      if (value != 0) text.push_back(static_cast<wchar_t>(value));
    }
    return text;
  }
  auto decode = [&bytes](UINT code_page, DWORD flags) {
    const int needed = MultiByteToWideChar(code_page, flags, bytes.data(),
                                            static_cast<int>(bytes.size()), nullptr, 0);
    std::wstring text(needed > 0 ? static_cast<std::size_t>(needed) : 0, L'\0');
    if (needed > 0) {
      MultiByteToWideChar(code_page, flags, bytes.data(), static_cast<int>(bytes.size()),
                          text.data(), needed);
    }
    return text;
  };
  std::wstring text = decode(CP_UTF8, MB_ERR_INVALID_CHARS);
  return text.empty() ? decode(CP_OEMCP, 0) : text;
}

}  // namespace

core::Status Start(const modules::ProcessRequest& request) {
  if (request.executable.empty()) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument, L"Process executable is required");
  }
  if (!request.elevated) return StartCreateProcess(request, nullptr);

  std::wstring executable;
  DHEPZ_RETURN_IF_ERROR(ResolveExecutable(request.executable, &executable));
  const std::wstring arguments = Arguments(request.arguments);
  SHELLEXECUTEINFOW execute{};
  execute.cbSize = sizeof(execute);
  execute.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
  execute.lpVerb = L"runas";
  execute.lpFile = executable.c_str();
  execute.lpParameters = arguments.empty() ? nullptr : arguments.c_str();
  execute.lpDirectory = request.working_directory.empty() ? nullptr : request.working_directory.c_str();
  execute.nShow = SW_SHOWNORMAL;
  if (!ShellExecuteExW(&execute)) {
    const DWORD error = GetLastError();
    return DHEPZ_ERR(error == ERROR_CANCELLED ? core::ErrorCode::Cancelled : core::ErrorCode::IoError,
                     L"Cannot start elevated process: " + ErrorText(error));
  }
  if (execute.hProcess != nullptr) CloseHandle(execute.hProcess);
  return core::Ok();
}

core::Status Run(const modules::ProcessRequest& request, std::wstring* standard_output) {
  if (standard_output == nullptr || request.executable.empty() || request.elevated) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument, L"Captured process arguments are invalid");
  }
  standard_output->clear();
  std::wstring executable;
  DHEPZ_RETURN_IF_ERROR(ResolveExecutable(request.executable, &executable));
  SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
  HANDLE read_pipe = nullptr;
  HANDLE write_pipe = nullptr;
  if (!CreatePipe(&read_pipe, &write_pipe, &security, 0) ||
      !SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0)) {
    if (read_pipe != nullptr) CloseHandle(read_pipe);
    if (write_pipe != nullptr) CloseHandle(write_pipe);
    return DHEPZ_ERR(core::ErrorCode::IoError, L"Cannot create process output pipe");
  }

  std::wstring command = CommandLine(request, executable);
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdOutput = write_pipe;
  startup.hStdError = write_pipe;
  startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  PROCESS_INFORMATION info{};
  const BOOL started = CreateProcessW(
      executable.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
      nullptr, request.working_directory.empty() ? nullptr : request.working_directory.c_str(),
      &startup, &info);
  CloseHandle(write_pipe);
  if (!started) {
    CloseHandle(read_pipe);
    return DHEPZ_ERR(core::ErrorCode::IoError,
                     L"Cannot run " + request.executable + L": " + ErrorText(GetLastError()));
  }
  CloseHandle(info.hThread);

  std::string bytes;
  char buffer[4096];
  for (;;) {
    DWORD available = 0;
    if (!PeekNamedPipe(read_pipe, nullptr, 0, nullptr, &available, nullptr)) break;
    if (available != 0) {
      DWORD received = 0;
      const DWORD wanted = (std::min)(available, static_cast<DWORD>(sizeof(buffer)));
      if (!ReadFile(read_pipe, buffer, wanted, &received, nullptr) || received == 0) break;
      bytes.append(buffer, received);
      continue;
    }
    if (WaitForSingleObject(info.hProcess, 10) == WAIT_OBJECT_0) break;
  }
  for (;;) {
    DWORD received = 0;
    if (!ReadFile(read_pipe, buffer, sizeof(buffer), &received, nullptr) || received == 0) break;
    bytes.append(buffer, received);
  }
  DWORD exit_code = 1;
  GetExitCodeProcess(info.hProcess, &exit_code);
  CloseHandle(info.hProcess);
  CloseHandle(read_pipe);
  *standard_output = DecodeOutput(bytes);
  if (exit_code != 0) {
    return DHEPZ_ERR(core::ErrorCode::IoError,
                     request.executable + L" exited with code " + std::to_wstring(exit_code));
  }
  return core::Ok();
}

}  // namespace process
