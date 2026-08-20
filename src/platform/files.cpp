#include "platform/files.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <string>

#include "platform/paths.h"
#include "platform/strings.h"

namespace files {
namespace {

constexpr std::int64_t kMaxReadableBytes = 64LL * 1024 * 1024;
constexpr std::size_t kChunkBytes = 1u << 20;

// System wording for a Win32 error, trimmed of the trailing CRLF that
// FORMAT_MESSAGE appends. Falls back to the bare code — a message is never
// worth failing for.
std::wstring ErrorText(DWORD code) {
  wchar_t* raw = nullptr;
  const DWORD len = FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, code, 0, reinterpret_cast<wchar_t*>(&raw), 0, nullptr);
  std::wstring text;
  if (len != 0 && raw != nullptr) {
    text.assign(raw, len);
    while (!text.empty() && (text.back() == L'\n' || text.back() == L'\r' || text.back() == L' ')) {
      text.pop_back();
    }
    LocalFree(raw);
  }
  if (text.empty()) {
    text = L"error " + std::to_wstring(code);
  }
  return text;
}

// Maps the codes callers actually branch on; everything else is IoError.
core::Status StatusFromWin32(std::wstring_view what, DWORD code) {
  std::wstring message = std::wstring(what) + L": " + ErrorText(code);
  switch (code) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
      return DHEPZ_ERR(core::ErrorCode::NotFound, std::move(message));
    case ERROR_FILE_EXISTS:
    case ERROR_ALREADY_EXISTS:
      return DHEPZ_ERR(core::ErrorCode::AlreadyExists, std::move(message));
    default:
      return DHEPZ_ERR(core::ErrorCode::IoError, std::move(message));
  }
}

core::Status EnsureParentExists(std::wstring_view target) {
  const std::wstring parent = paths::Parent(target);
  if (!parent.empty() && !paths::EnsureDirectory(parent)) {
    return DHEPZ_ERR(core::ErrorCode::IoError, L"Cannot create folder: " + parent);
  }
  return core::Ok();
}

core::Status WriteAllBytes(HANDLE handle, std::string_view utf8, std::wstring_view what) {
  std::size_t written_total = 0;
  while (written_total < utf8.size()) {
    DWORD written = 0;
    const DWORD want =
        static_cast<DWORD>((std::min)(utf8.size() - written_total, kChunkBytes));
    if (!WriteFile(handle, utf8.data() + written_total, want, &written, nullptr)) {
      return StatusFromWin32(what, GetLastError());
    }
    if (written == 0) {
      return DHEPZ_ERR(core::ErrorCode::IoError, std::wstring(what) + L": no bytes were written");
    }
    written_total += written;
  }
  return core::Ok();
}

}  // namespace

core::Status ReadText(std::wstring_view path, std::wstring* out) {
  if (out == nullptr) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument, L"ReadText requires an output string");
  }
  out->clear();

  const std::wstring file(path);
  HANDLE handle = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return StatusFromWin32(L"Cannot open file", GetLastError());
  }

  core::Status status;
  std::string raw;
  LARGE_INTEGER size{};
  if (!GetFileSizeEx(handle, &size)) {
    status = StatusFromWin32(L"Cannot read file size", GetLastError());
  } else if (size.QuadPart > kMaxReadableBytes) {
    status = DHEPZ_ERR(core::ErrorCode::IoError, L"File is too large to open (over 64 MB)");
  } else {
    raw.resize(static_cast<std::size_t>(size.QuadPart), '\0');
    std::size_t read_total = 0;
    while (read_total < raw.size()) {
      DWORD chunk = 0;
      const DWORD want = static_cast<DWORD>((std::min)(raw.size() - read_total, kChunkBytes));
      if (!ReadFile(handle, raw.data() + read_total, want, &chunk, nullptr)) {
        status = StatusFromWin32(L"Cannot read file", GetLastError());
        break;
      }
      if (chunk == 0) {
        break;
      }
      read_total += chunk;
    }
    raw.resize(read_total);
  }
  CloseHandle(handle);
  if (!status.ok()) {
    return status;
  }

  std::string_view body(raw);
  if (body.size() >= 3 && static_cast<unsigned char>(body[0]) == 0xEF &&
      static_cast<unsigned char>(body[1]) == 0xBB && static_cast<unsigned char>(body[2]) == 0xBF) {
    body.remove_prefix(3);
  }
  return str::FromUtf8(body, out);
}

core::Status WriteTextAtomic(std::wstring_view path, std::wstring_view text) {
  const std::wstring target(path);
  DHEPZ_RETURN_IF_ERROR(EnsureParentExists(target));

  // Fixed temp name is safe under the single-owner-thread contract: only one
  // thread writes app files, so two concurrent saves to the same target
  // cannot race on <target>.tmp. Revisit (random suffix) if that changes.
  const std::wstring temp = target + L".tmp";

  std::string utf8;
  DHEPZ_RETURN_IF_ERROR(str::ToUtf8(text, &utf8));

  HANDLE handle =
      CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return StatusFromWin32(L"Cannot create temporary file", GetLastError());
  }
  core::Status status = WriteAllBytes(handle, utf8, L"Cannot write temporary file");
  if (status.ok() && !FlushFileBuffers(handle)) {
    status = StatusFromWin32(L"Cannot flush temporary file", GetLastError());
  }
  CloseHandle(handle);
  if (!status.ok()) {
    DeleteFileW(temp.c_str());
    return status;
  }

  if (paths::FileExists(target)) {
    if (!ReplaceFileW(target.c_str(), temp.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS,
                      nullptr, nullptr)) {
      const DWORD code = GetLastError();
      if (!MoveFileExW(temp.c_str(), target.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temp.c_str());
        return StatusFromWin32(L"Cannot replace file", code);
      }
    }
  } else if (!MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH)) {
    const DWORD code = GetLastError();
    DeleteFileW(temp.c_str());
    return StatusFromWin32(L"Cannot move file into place", code);
  }
  return core::Ok();
}

core::Status WriteTextNew(std::wstring_view path, std::wstring_view text) {
  const std::wstring target(path);
  DHEPZ_RETURN_IF_ERROR(EnsureParentExists(target));

  std::string utf8;
  DHEPZ_RETURN_IF_ERROR(str::ToUtf8(text, &utf8));

  // CREATE_NEW means an existing target is never touched: no replace, no
  // rename, no delete, so a file already open in an editor keeps its identity.
  HANDLE handle =
      CreateFileW(target.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return StatusFromWin32(L"Cannot create file", GetLastError());
  }

  core::Status status = WriteAllBytes(handle, utf8, L"Cannot write file");
  if (status.ok() && !FlushFileBuffers(handle)) {
    status = StatusFromWin32(L"Cannot flush file", GetLastError());
  }
  CloseHandle(handle);
  return status;
}

core::Status WriteTextInPlace(std::wstring_view path, std::wstring_view text) {
  const std::wstring target(path);
  DHEPZ_RETURN_IF_ERROR(EnsureParentExists(target));

  std::string utf8;
  DHEPZ_RETURN_IF_ERROR(str::ToUtf8(text, &utf8));

  // OPEN_ALWAYS preserves an existing file object and creates the first file
  // when there is none. FILE_SHARE_READ keeps an external editor able to
  // read while this handle is open; no replace/rename/delete occurs.
  HANDLE handle = CreateFileW(target.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return StatusFromWin32(L"Cannot open file for writing", GetLastError());
  }

  LARGE_INTEGER start{};
  core::Status status;
  if (!SetFilePointerEx(handle, start, nullptr, FILE_BEGIN)) {
    status = StatusFromWin32(L"Cannot seek file", GetLastError());
  } else {
    status = WriteAllBytes(handle, utf8, L"Cannot write file");
  }
  // The new content can be shorter than the old. Without this, stale
  // trailing bytes remain and make the next parse fail despite a successful
  // write — the bug this mode exists to prevent.
  if (status.ok() && !SetEndOfFile(handle)) {
    status = StatusFromWin32(L"Cannot truncate file", GetLastError());
  }
  if (status.ok() && !FlushFileBuffers(handle)) {
    status = StatusFromWin32(L"Cannot flush file", GetLastError());
  }
  CloseHandle(handle);
  return status;
}

std::wstring BackupPath(std::wstring_view path) { return std::wstring(path) + L".otn.bak"; }

core::Status MakeBackup(std::wstring_view path) {
  if (!paths::FileExists(path)) {
    return core::Ok();
  }
  const std::wstring source(path);
  const std::wstring backup = BackupPath(path);
  if (!CopyFileW(source.c_str(), backup.c_str(), FALSE)) {
    return StatusFromWin32(L"Cannot create backup", GetLastError());
  }
  return core::Ok();
}

bool HasBackup(std::wstring_view path) { return paths::FileExists(BackupPath(path)); }

core::Status RestoreBackup(std::wstring_view path) {
  const std::wstring backup = BackupPath(path);
  if (!paths::FileExists(backup)) {
    return DHEPZ_ERR(core::ErrorCode::NotFound, L"No backup exists yet for " + std::wstring(path));
  }
  std::wstring text;
  DHEPZ_RETURN_IF_ERROR(ReadText(backup, &text));
  return WriteTextAtomic(path, text);
}

}  // namespace files
