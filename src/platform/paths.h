// Filesystem paths and the installed-app directory split.
//
// Two locations, kept strictly separate (see plan.md Part 4):
//
//   - ExecutableDir() — the install directory. Treated as READ-ONLY at
//     runtime. Nothing in the process may write next to the EXE: an update
//     replaces the install directory wholesale, so any file written there
//     would be destroyed or left orphaned.
//
//   - StateDir() — %LOCALAPPDATA%\dhepz\state. All user state lives here
//     and survives every update. The directory is never created until state
//     actually must be written (G1): a tray-only startup that never opens a
//     window must not touch the disk. StateDir() itself is pure; only
//     EnsureStateDir() creates anything.
//
// Normalize and ValidateUnderRoot are the security half of this layer:
// paths derived from JSON config or user input reach CreateProcess, so they
// are canonicalised and checked against an expected root before use.
//
// Like all of platform/, this layer may use Windows but keeps windows.h out
// of the header so callers do not pull it in transitively.
#pragma once

#include <string>
#include <string_view>

#include "core/status.h"

namespace paths {

// Full path of the running executable, and its directory. Read-only at
// runtime — see the header comment before writing anywhere near it.
std::wstring ExecutablePath();
std::wstring ExecutableDir();

// The LocalAppData known folder (the API equivalent of %LOCALAPPDATA%, and
// the one that stays correct under folder redirection).
std::wstring LocalAppDataDir();

// %LOCALAPPDATA%\dhepz\state, without a trailing separator. Pure: this call
// never touches the disk, so a tray-only startup costs nothing (G1).
std::wstring StateDir();

// Creates StateDir() and any missing parents. The only function in this
// layer that creates the state directory; call it just before the first
// write, never at startup.
core::Status EnsureStateDir();

// Joins two segments with exactly one separator between them. Either side
// may be empty, in which case the other is returned as written.
std::wstring Join(std::wstring_view a, std::wstring_view b);

// Everything before the last separator, or {} when there is none.
std::wstring Parent(std::wstring_view path);

// Everything after the last separator, or the whole input when there is none.
std::wstring FileName(std::wstring_view path);

bool FileExists(std::wstring_view path);
bool DirectoryExists(std::wstring_view path);

// Creates every missing component of `path`. Returns true when the directory
// exists after the call.
bool EnsureDirectory(std::wstring_view path);

// Canonicalises an absolute path: trims whitespace, expands %VAR%
// references, converts `/` to `\`, and resolves `.` and `..` segments via
// GetFullPathNameW (with retry-on-too-small-buffer). The result has no
// trailing separator except for a drive root such as C:\.
//
// The input must be absolute — a drive-letter path (C:\...) or a UNC path
// (\\server\share...). Relative input is rejected with an empty result,
// because resolving it would silently depend on the process CWD. Callers
// with config- or user-derived paths should go through ValidateUnderRoot,
// which pins the base explicitly.
//
// Long-path aware: the manifest declares longPathAware, so results past
// MAX_PATH are returned as-is; no \\?\ prefixing is needed on Windows 10
// 1607+ for the Win32 file APIs.
//
// Returns {} when the input is empty, relative, or otherwise unusable.
std::wstring Normalize(std::wstring_view path);

// Checks that `path` stays inside `root`, for anything derived from config
// or user input. Both sides are canonicalised with Normalize, so `..`
// traversal, mixed separators, %VAR% tricks and case variations of the root
// itself are all handled. Comparison is case-insensitive, as NTFS is.
//
// A path equal to `root` is contained; a sibling or an escape is not.
// The normalised path is returned in *out_full when it is contained.
//
//   InvalidArgument — either argument is empty or not an absolute path.
//   PermissionDenied — the normalised path escapes the root.
core::Status ValidateUnderRoot(std::wstring_view root, std::wstring_view path,
                               std::wstring* out_full);

}  // namespace paths
