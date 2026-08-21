#include "platform/process.h"

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

#include <cstring>
#include <vector>

#include "platform/strings.h"

namespace process {
namespace {

// wsl.exe writes its own messages (--list, --status) as UTF-16LE while
// piping an inner command's stdout through unchanged as UTF-8. Sniff
// instead of assuming: a BOM, or NULs in the odd byte positions, means
// UTF-16LE.
std::wstring DecodeOutput(const std::string& raw) {
  if (raw.size() >= 2 && static_cast<unsigned char>(raw[0]) == 0xFF &&
      static_cast<unsigned char>(raw[1]) == 0xFE) {
    const size_t units = (raw.size() - 2) / sizeof(wchar_t);
    std::wstring out(units, L'\0');
    memcpy(out.data(), raw.data() + 2, units * sizeof(wchar_t));
    return out;
  }
  if (raw.size() >= 4 && (raw.size() % 2) == 0) {
    size_t odd_nulls = 0;
    size_t odd_total = 0;
    for (size_t i = 1; i < raw.size(); i += 2) {
      ++odd_total;
      if (raw[i] == '\0') ++odd_nulls;
    }
    if (odd_total > 0 && odd_nulls * 4 >= odd_total * 3) {
      const size_t units = raw.size() / sizeof(wchar_t);
      std::wstring out(units, L'\0');
      memcpy(out.data(), raw.data(), units * sizeof(wchar_t));
      return out;
    }
  }
  std::wstring out;
  if (!str::FromUtf8(raw, &out).ok()) return std::wstring();
  return out;
}

void EnsureCom() {
  static const bool initialized = [] {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    return true;
  }();
  (void)initialized;
}

}  // namespace

std::wstring ErrorMessage(unsigned long code) {
  LPWSTR buffer = nullptr;
  const DWORD length =
      FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                         FORMAT_MESSAGE_IGNORE_INSERTS,
                     nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                     reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
  std::wstring message;
  if (length != 0 && buffer != nullptr) {
    message.assign(buffer, length);
    message = str::Trim(message);
  }
  if (buffer != nullptr) LocalFree(buffer);
  if (message.empty()) message = L"error " + std::to_wstring(code);
  return message;
}

core::Status Launch(std::wstring_view exe, std::wstring_view command_line,
                    std::wstring_view working_dir, WindowMode window) {
  std::wstring mutable_cmd(command_line);
  std::wstring exe_storage(exe);
  std::wstring dir_storage(working_dir);

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  if (window == WindowMode::Hidden) {
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
  }
  PROCESS_INFORMATION pi{};
  const DWORD flags = CREATE_UNICODE_ENVIRONMENT |
                      (window == WindowMode::Hidden ? CREATE_NO_WINDOW : CREATE_NEW_CONSOLE);
  const BOOL ok = CreateProcessW(exe_storage.empty() ? nullptr : exe_storage.c_str(),
                                 mutable_cmd.empty() ? nullptr : mutable_cmd.data(), nullptr,
                                 nullptr, FALSE, flags, nullptr,
                                 dir_storage.empty() ? nullptr : dir_storage.c_str(), &si, &pi);
  if (!ok) {
    return core::Err(core::ErrorCode::IoError,
                     L"launch failed: " + ErrorMessage(GetLastError()));
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return core::Ok();
}

core::Status ShellLaunch(std::wstring_view verb, std::wstring_view file,
                         std::wstring_view parameters, std::wstring_view working_dir) {
  // Shell verbs go through COM handlers, so the apartment must exist; it is
  // created on demand, never on the startup path.
  EnsureCom();

  const std::wstring verb_storage(verb);
  const std::wstring file_storage(file);
  const std::wstring params_storage(parameters);
  const std::wstring dir_storage(working_dir);

  SHELLEXECUTEINFOW info{};
  info.cbSize = sizeof(info);
  info.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
  info.lpVerb = verb_storage.empty() ? nullptr : verb_storage.c_str();
  info.lpFile = file_storage.c_str();
  info.lpParameters = params_storage.empty() ? nullptr : params_storage.c_str();
  info.lpDirectory = dir_storage.empty() ? nullptr : dir_storage.c_str();
  info.nShow = SW_SHOWNORMAL;

  if (!ShellExecuteExW(&info)) {
    const DWORD code = GetLastError();
    if (code == ERROR_CANCELLED) {
      return core::Err(core::ErrorCode::Cancelled, L"Cancelled by the user.");
    }
    return core::Err(core::ErrorCode::IoError, L"shell launch failed: " + ErrorMessage(code));
  }
  if (info.hProcess != nullptr) CloseHandle(info.hProcess);
  return core::Ok();
}

core::Status RunCapture(std::wstring_view command_line, std::wstring_view working_dir,
                        unsigned long timeout_ms, RunResult* out,
                        const std::atomic<bool>* cancelled) {
  if (out != nullptr) *out = RunResult{};

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;

  HANDLE read_end = nullptr;
  HANDLE write_end = nullptr;
  if (!CreatePipe(&read_end, &write_end, &sa, 0)) {
    return core::Err(core::ErrorCode::IoError,
                     L"pipe creation failed: " + ErrorMessage(GetLastError()));
  }
  SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);

  std::wstring mutable_cmd(command_line);
  std::wstring dir_storage(working_dir);

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  si.hStdOutput = write_end;
  si.hStdError = write_end;
  si.hStdInput = nullptr;

  PROCESS_INFORMATION pi{};
  const BOOL ok = CreateProcessW(nullptr, mutable_cmd.data(), nullptr, nullptr, TRUE,
                                 CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW, nullptr,
                                 dir_storage.empty() ? nullptr : dir_storage.c_str(), &si, &pi);
  CloseHandle(write_end);
  if (!ok) {
    CloseHandle(read_end);
    return core::Err(core::ErrorCode::IoError,
                     L"capture spawn failed: " + ErrorMessage(GetLastError()));
  }
  CloseHandle(pi.hThread);

  // Deadline-bounded drain: a child that keeps the write end open must not
  // hang us past timeout_ms. PeekNamedPipe polls without blocking in
  // ReadFile, and the deadline is checked between polls.
  const ULONGLONG deadline = GetTickCount64() + timeout_ms;
  std::string raw;
  char buffer[4096];
  bool timed_out = false;
  bool was_cancelled = false;
  for (;;) {
    if (cancelled != nullptr && cancelled->load()) {
      was_cancelled = true;
      break;
    }
    DWORD available = 0;
    if (!PeekNamedPipe(read_end, nullptr, 0, nullptr, &available, nullptr)) break;
    if (available > 0) {
      DWORD read = 0;
      if (!ReadFile(read_end, buffer, sizeof(buffer), &read, nullptr) || read == 0) break;
      raw.append(buffer, read);
      if (raw.size() > 4u * 1024 * 1024) break;  // bounded capture
      continue;
    }
    if (WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0) {
      DWORD remaining = 0;
      if (PeekNamedPipe(read_end, nullptr, 0, nullptr, &remaining, nullptr) && remaining > 0) {
        continue;
      }
      break;
    }
    if (GetTickCount64() >= deadline) {
      timed_out = true;
      break;
    }
    Sleep(15);
  }
  CloseHandle(read_end);

  if (timed_out || was_cancelled) {
    TerminateProcess(pi.hProcess, 1);
    WaitForSingleObject(pi.hProcess, 2000);
    CloseHandle(pi.hProcess);
    if (out != nullptr) {
      out->timed_out = timed_out;
      out->output = DecodeOutput(raw);
    }
    if (was_cancelled) {
      return core::Err(core::ErrorCode::Cancelled, L"Process capture cancelled");
    }
    return core::Ok();
  }

  const DWORD wait = WaitForSingleObject(pi.hProcess, timeout_ms);
  if (wait == WAIT_TIMEOUT) {
    TerminateProcess(pi.hProcess, 1);
    CloseHandle(pi.hProcess);
    if (out != nullptr) out->timed_out = true;
    return core::Ok();
  }
  DWORD code = 0;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hProcess);

  if (out != nullptr) {
    out->exit_code = static_cast<int>(code);
    out->output = DecodeOutput(raw);
  }
  return core::Ok();
}

}  // namespace process
