// File IO with three deliberately different write modes.
//
// The modes exist because they have genuinely different failure semantics,
// and collapsing them caused a real bug in the old build:
//
//   - WriteTextAtomic  — temp file + flush + replace. A crash mid-write can
//     never leave a corrupt settings.json: the target either keeps its old
//     bytes or gains the complete new ones.
//   - WriteTextNew     — CREATE_NEW. Fails when the target exists, for cases
//     where clobbering is itself the bug. Never replaces, renames, or
//     deletes an existing file.
//   - WriteTextInPlace — writes through the file's current object, then
//     truncates with SetEndOfFile and flushes. The file identity (its
//     directory entry and open handles) never changes, so an external
//     editor watching the file keeps working. The truncation is the part
//     the old build got wrong: shorter new content left stale trailing
//     bytes behind, and the next parse read garbage.
//
// All writes are UTF-8 without BOM. Reads tolerate a UTF-8 BOM and strip
// it — stripping belongs here, not in str::FromUtf8, which preserves it as
// U+FEFF (pinned by #9's tests). Input that is not valid UTF-8 is a
// ParseError, never a silent empty result: the old build returned {} on
// failure, so one bad byte in a config file came up as a blank UI with
// nothing to explain it.
//
// Like all of platform/, this layer may use Windows but keeps windows.h out
// of the header.
#pragma once

#include <string>
#include <string_view>

#include "core/status.h"

namespace files {

// Reads a whole file as UTF-8 text. Files over 64 MiB are refused as
// IoError — config-sized files only; anything bigger is a mistake or the
// wrong API.
//
//   NotFound       — the file does not exist.
//   IoError        — open, size, or read failure; file too large.
//   ParseError     — the bytes are not valid UTF-8.
core::Status ReadText(std::wstring_view path, std::wstring* out);

// Writes `text` to "<path>.tmp", flushes it, then replaces the target in
// one step (ReplaceFileW, falling back to MoveFileExW). Creates missing
// parent directories. On any failure the target is untouched and the temp
// file is removed.
core::Status WriteTextAtomic(std::wstring_view path, std::wstring_view text);

// Creates `path` and writes `text`. Fails without touching anything when
// the file already exists.
//
//   AlreadyExists  — the target is present.
core::Status WriteTextNew(std::wstring_view path, std::wstring_view text);

// Overwrites the bytes of an existing file through its current file object,
// truncates, and flushes. Never replaces, renames, or deletes the target;
// creates it when it does not exist yet. FILE_SHARE_READ is kept so an
// external editor can still read while the write is in flight.
core::Status WriteTextInPlace(std::wstring_view path, std::wstring_view text);

// Backup path used before destructive writes: "<path>.otn.bak".
std::wstring BackupPath(std::wstring_view path);

// Copies path -> BackupPath(path). A missing source is not an error — there
// is simply nothing to back up yet.
core::Status MakeBackup(std::wstring_view path);
bool HasBackup(std::wstring_view path);

// Replaces `path` with its backup, atomically. Fails with NotFound when no
// backup exists.
core::Status RestoreBackup(std::wstring_view path);

}  // namespace files
