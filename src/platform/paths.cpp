#include "platform/paths.h"

#include <windows.h>
#include <shlobj.h>

#include "platform/strings.h"

namespace paths {
namespace {

std::wstring KnownFolder(REFKNOWNFOLDERID id) {
  PWSTR raw = nullptr;
  std::wstring result;
  if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &raw)) && raw != nullptr) {
    result.assign(raw);
  }
  if (raw != nullptr) {
    CoTaskMemFree(raw);
  }
  return result;
}

std::wstring ExpandEnvironment(std::wstring_view value) {
  const std::wstring input(value);
  if (input.find(L'%') == std::wstring::npos) {
    return input;
  }
  std::wstring buffer(MAX_PATH, L'\0');
  for (;;) {
    const DWORD needed =
        ExpandEnvironmentStringsW(input.c_str(), buffer.data(), static_cast<DWORD>(buffer.size()));
    if (needed == 0) {
      return input;
    }
    if (needed <= buffer.size()) {
      buffer.resize(needed > 0 ? needed - 1 : 0);
      return buffer;
    }
    buffer.resize(needed);
  }
}

// Drive-letter (C:\...), device (\\?\...), or UNC (\\server\...). Anything
// else — including a bare "\foo" — would resolve against the process CWD or
// its drive inside GetFullPathNameW, which is exactly the hidden dependency
// Normalize refuses to introduce. Input is slash-normalised before this runs.
bool IsAbsolute(std::wstring_view path) {
  if (path.size() >= 3 && (path[1] == L':') && (path[2] == L'\\')) {
    const wchar_t d = path[0];
    return (d >= L'A' && d <= L'Z') || (d >= L'a' && d <= L'z');
  }
  return path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\';
}

}  // namespace

std::wstring ExecutablePath() {
  std::wstring buffer(MAX_PATH, L'\0');
  for (;;) {
    const DWORD written =
        GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (written == 0) {
      return {};
    }
    if (written < buffer.size()) {
      buffer.resize(written);
      return buffer;
    }
    buffer.resize(buffer.size() * 2);
  }
}

std::wstring ExecutableDir() { return Parent(ExecutablePath()); }

std::wstring LocalAppDataDir() { return KnownFolder(FOLDERID_LocalAppData); }

std::wstring StateDir() {
  const std::wstring base = LocalAppDataDir();
  if (base.empty()) {
    return {};
  }
  return Join(Join(base, L"dhepz"), L"state");
}

core::Status EnsureStateDir() {
  const std::wstring dir = StateDir();
  if (dir.empty()) {
    return DHEPZ_ERR(core::ErrorCode::IoError, L"LocalAppData folder could not be resolved");
  }
  if (EnsureDirectory(dir)) {
    return core::Ok();
  }
  return DHEPZ_ERR(core::ErrorCode::IoError, L"Could not create state directory " + dir);
}

std::wstring Join(std::wstring_view a, std::wstring_view b) {
  if (a.empty()) {
    return std::wstring(b);
  }
  if (b.empty()) {
    return std::wstring(a);
  }
  std::wstring out(a);
  if (out.back() != L'\\' && out.back() != L'/') {
    out.push_back(L'\\');
  }
  std::size_t skip = 0;
  while (skip < b.size() && (b[skip] == L'\\' || b[skip] == L'/')) {
    ++skip;
  }
  out.append(b.substr(skip));
  return out;
}

std::wstring Parent(std::wstring_view path) {
  const std::size_t cut = path.find_last_of(L"\\/");
  if (cut == std::wstring_view::npos) {
    return {};
  }
  if (cut == 0) {
    return L"\\";
  }
  return std::wstring(path.substr(0, cut));
}

std::wstring FileName(std::wstring_view path) {
  const std::size_t cut = path.find_last_of(L"\\/");
  if (cut == std::wstring_view::npos) {
    return std::wstring(path);
  }
  return std::wstring(path.substr(cut + 1));
}

bool FileExists(std::wstring_view path) {
  if (path.empty()) {
    return false;
  }
  const DWORD attrs = GetFileAttributesW(std::wstring(path).c_str());
  return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool DirectoryExists(std::wstring_view path) {
  if (path.empty()) {
    return false;
  }
  const DWORD attrs = GetFileAttributesW(std::wstring(path).c_str());
  return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool EnsureDirectory(std::wstring_view path) {
  if (path.empty()) {
    return false;
  }
  if (DirectoryExists(path)) {
    return true;
  }
  const std::wstring parent = Parent(path);
  if (!parent.empty() && parent != path && !DirectoryExists(parent)) {
    if (!EnsureDirectory(parent)) {
      return false;
    }
  }
  if (CreateDirectoryW(std::wstring(path).c_str(), nullptr)) {
    return true;
  }
  return GetLastError() == ERROR_ALREADY_EXISTS;
}

std::wstring Normalize(std::wstring_view path) {
  if (path.empty()) {
    return {};
  }
  std::wstring input = ExpandEnvironment(str::Trim(path));
  if (input.empty()) {
    return {};
  }
  for (wchar_t& c : input) {
    if (c == L'/') {
      c = L'\\';
    }
  }
  // Reject relative input before GetFullPathNameW can resolve it against the
  // process CWD: a result that silently depends on where the app was started
  // from is worse than no result.
  if (!IsAbsolute(input)) {
    return {};
  }
  std::wstring buffer(MAX_PATH, L'\0');
  for (;;) {
    const DWORD needed =
        GetFullPathNameW(input.c_str(), static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
    if (needed == 0) {
      break;
    }
    if (needed < buffer.size()) {
      buffer.resize(needed);
      input = buffer;
      break;
    }
    buffer.resize(needed);
  }
  // Strip trailing separators, but never off a drive root: "C:\" is size 3.
  while (input.size() > 3 && input.back() == L'\\') {
    input.pop_back();
  }
  return input;
}

core::Status ValidateUnderRoot(std::wstring_view root, std::wstring_view path,
                               std::wstring* out_full) {
  if (out_full == nullptr) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                     L"ValidateUnderRoot requires an output string");
  }
  out_full->clear();

  const std::wstring base = Normalize(root);
  if (base.empty()) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                     L"Root is empty or not an absolute path: " + std::wstring(root));
  }
  const std::wstring full = Normalize(path);
  if (full.empty()) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                     L"Path is empty or not an absolute path: " + std::wstring(path));
  }

  // Case-insensitive ordinal compare: NTFS resolves names case-insensitively,
  // so "C:\Root" and "c:\root" are the same place. CSTR_LESS_THAN/EQUAL/GREATER
  // are ordinal positions, not error codes, so every branch is handled.
  const auto relate = [&base](std::wstring_view candidate) {
    return CompareStringOrdinal(candidate.data(), static_cast<int>(candidate.size()), base.data(),
                                static_cast<int>(base.size()), TRUE);
  };

  const bool contained =
      relate(full) == CSTR_EQUAL ||
      (full.size() > base.size() && full[base.size()] == L'\\' &&
       relate(std::wstring_view(full).substr(0, base.size())) == CSTR_EQUAL);

  if (!contained) {
    return DHEPZ_ERR(core::ErrorCode::PermissionDenied,
                     L"Path escapes its expected root: " + full + L" is not under " + base);
  }
  *out_full = full;
  return core::Ok();
}

}  // namespace paths
