#include "platform/files.h"

#include <windows.h>

#include <filesystem>
#include <string>

#include "framework/test_case.h"

namespace {

// A scratch directory under %TEMP% unique to this process. Every test gets
// the directory created on demand and removes it at the end, so no state
// leaks between tests.
std::wstring TestRoot() {
  wchar_t buffer[MAX_PATH] = {};
  const DWORD len = GetTempPathW(MAX_PATH, buffer);
  std::wstring root(buffer, len);
  root += L"dhepz_files_test_";
  root += std::to_wstring(GetCurrentProcessId());
  return root;
}

std::wstring MakeTestDir() {
  const std::wstring root = TestRoot();
  std::filesystem::create_directories(root);
  return root;
}

void RemoveTestDir() { std::filesystem::remove_all(TestRoot()); }

// Raw byte helpers: the tests must be able to see exactly what is on disk
// (BOM presence, line endings, trailing bytes), which the layer under test
// would happily normalise away.
std::string ReadRawBytes(const std::wstring& path) {
  HANDLE handle =
      CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                  FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return {};
  }
  std::string raw;
  char chunk[4096];
  DWORD got = 0;
  while (ReadFile(handle, chunk, sizeof(chunk), &got, nullptr) && got > 0) {
    raw.append(chunk, got);
  }
  CloseHandle(handle);
  return raw;
}

bool WriteRawBytes(const std::wstring& path, const std::string& bytes) {
  HANDLE handle =
      CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return false;
  }
  DWORD written = 0;
  const bool ok = WriteFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
                  written == bytes.size();
  CloseHandle(handle);
  return ok;
}

bool StartsWithBom(const std::string& raw) {
  return raw.size() >= 3 && static_cast<unsigned char>(raw[0]) == 0xEF &&
         static_cast<unsigned char>(raw[1]) == 0xBB && static_cast<unsigned char>(raw[2]) == 0xBF;
}

}  // namespace

DHEPZ_TEST(Files, AtomicRoundTripAndNoBomNoCr) {
  const std::wstring dir = MakeTestDir();
  const std::wstring path = dir + L"\\settings.json";

  const std::wstring text = L"{\n  \"theme\": \"dark\",\n  \"note\": \"caf\u00e9 \u4e2d\u6587\"\n}\n";
  DHEPZ_CHECK(files::WriteTextAtomic(path, text).ok());

  std::wstring back;
  DHEPZ_CHECK(files::ReadText(path, &back).ok());
  DHEPZ_CHECK_EQ(back, text);

  // UTF-8 without BOM and LF untouched: no CR byte anywhere, no BOM prefix.
  const std::string raw = ReadRawBytes(path);
  DHEPZ_CHECK(!raw.empty());
  DHEPZ_CHECK(!StartsWithBom(raw));
  DHEPZ_CHECK(raw.find('\r') == std::string::npos);

  // A second atomic write replaces cleanly and leaves no temp file behind.
  DHEPZ_CHECK(files::WriteTextAtomic(path, L"second").ok());
  DHEPZ_CHECK(files::ReadText(path, &back).ok());
  DHEPZ_CHECK_EQ(back, std::wstring(L"second"));
  DHEPZ_CHECK(!std::filesystem::exists(path + L".tmp"));

  RemoveTestDir();
}

DHEPZ_TEST(Files, AtomicCreatesMissingParents) {
  const std::wstring dir = MakeTestDir();
  const std::wstring path = dir + L"\\a\\b\\c\\deep.txt";
  DHEPZ_CHECK(files::WriteTextAtomic(path, L"deep").ok());
  std::wstring back;
  DHEPZ_CHECK(files::ReadText(path, &back).ok());
  DHEPZ_CHECK_EQ(back, std::wstring(L"deep"));
  RemoveTestDir();
}

// The criterion's second case: a temp file left behind by an earlier failed
// run must not break the next write — it is simply overwritten on the way
// to replacing the target.
DHEPZ_TEST(Files, AtomicSurvivesStaleTempFile) {
  const std::wstring dir = MakeTestDir();
  const std::wstring path = dir + L"\\settings.json";
  DHEPZ_CHECK(files::WriteTextAtomic(path, L"original").ok());
  DHEPZ_CHECK(WriteRawBytes(path + L".tmp", "garbage from a crashed write"));

  DHEPZ_CHECK(files::WriteTextAtomic(path, L"recovered").ok());
  std::wstring back;
  DHEPZ_CHECK(files::ReadText(path, &back).ok());
  DHEPZ_CHECK_EQ(back, std::wstring(L"recovered"));
  DHEPZ_CHECK(!std::filesystem::exists(path + L".tmp"));
  RemoveTestDir();
}

// The criterion's first case, the other half: when the write itself fails,
// the original file is untouched. Making the target's parent a *file* forces
// EnsureDirectory to fail deterministically without any mocking.
DHEPZ_TEST(Files, AtomicFailureLeavesOriginalIntact) {
  const std::wstring dir = MakeTestDir();
  const std::wstring blocker = dir + L"\\blocker";
  DHEPZ_CHECK(WriteRawBytes(blocker, "i am a file, not a folder"));

  const std::wstring target_under_blocker = blocker + L"\\settings.json";
  const core::Status status = files::WriteTextAtomic(target_under_blocker, L"nope");
  DHEPZ_CHECK_FALSE(status.ok());
  DHEPZ_CHECK(!std::filesystem::exists(target_under_blocker));

  // And a target that does exist survives a failed write elsewhere intact.
  const std::wstring existing = dir + L"\\existing.json";
  DHEPZ_CHECK(files::WriteTextAtomic(existing, L"keep me").ok());
  DHEPZ_CHECK_FALSE(files::WriteTextAtomic(existing + L"\\child.txt", L"nope").ok());
  std::wstring back;
  DHEPZ_CHECK(files::ReadText(existing, &back).ok());
  DHEPZ_CHECK_EQ(back, std::wstring(L"keep me"));
  RemoveTestDir();
}

// The recorded bug in the old build: shorter new content left the tail of
// the old content behind, and the next parse read garbage.
DHEPZ_TEST(Files, InPlaceTruncatesShorterContent) {
  const std::wstring dir = MakeTestDir();
  const std::wstring path = dir + L"\\ui.json";

  const std::wstring long_text(4096, L'x');
  DHEPZ_CHECK(files::WriteTextInPlace(path, long_text).ok());

  DHEPZ_CHECK(files::WriteTextInPlace(path, L"short").ok());
  const std::string raw = ReadRawBytes(path);
  DHEPZ_CHECK_EQ(raw.size(), static_cast<std::size_t>(5));
  DHEPZ_CHECK_EQ(raw, std::string("short"));

  std::wstring back;
  DHEPZ_CHECK(files::ReadText(path, &back).ok());
  DHEPZ_CHECK_EQ(back, std::wstring(L"short"));
  RemoveTestDir();
}

DHEPZ_TEST(Files, InPlaceCreatesWhenMissingAndGrows) {
  const std::wstring dir = MakeTestDir();
  const std::wstring path = dir + L"\\fresh.json";
  DHEPZ_CHECK(files::WriteTextInPlace(path, L"one").ok());
  DHEPZ_CHECK(files::WriteTextInPlace(path, L"one but longer").ok());
  std::wstring back;
  DHEPZ_CHECK(files::ReadText(path, &back).ok());
  DHEPZ_CHECK_EQ(back, std::wstring(L"one but longer"));
  RemoveTestDir();
}

DHEPZ_TEST(Files, NewRefusesToClobber) {
  const std::wstring dir = MakeTestDir();
  const std::wstring path = dir + L"\\first.json";

  DHEPZ_CHECK(files::WriteTextNew(path, L"original").ok());
  const core::Status second = files::WriteTextNew(path, L"replacement");
  DHEPZ_CHECK_FALSE(second.ok());
  DHEPZ_CHECK_EQ(second.Code(), core::ErrorCode::AlreadyExists);

  std::wstring back;
  DHEPZ_CHECK(files::ReadText(path, &back).ok());
  DHEPZ_CHECK_EQ(back, std::wstring(L"original"));
  RemoveTestDir();
}

DHEPZ_TEST(Files, ReadStripsBom) {
  const std::wstring dir = MakeTestDir();
  const std::wstring path = dir + L"\\bom.txt";
  DHEPZ_CHECK(WriteRawBytes(path, std::string("\xEF\xBB\xBF") + "bom content"));

  std::wstring back;
  DHEPZ_CHECK(files::ReadText(path, &back).ok());
  DHEPZ_CHECK_EQ(back, std::wstring(L"bom content"));
  RemoveTestDir();
}

// The old build returned an empty string here, indistinguishable from a
// successful read of an empty file. Invalid bytes are a ParseError.
DHEPZ_TEST(Files, ReadRejectsInvalidUtf8) {
  const std::wstring dir = MakeTestDir();
  const std::wstring path = dir + L"\\bad.txt";
  DHEPZ_CHECK(WriteRawBytes(path, std::string("ok \xC3\x28 bad")));

  std::wstring back = L"stale";
  const core::Status status = files::ReadText(path, &back);
  DHEPZ_CHECK_FALSE(status.ok());
  DHEPZ_CHECK_EQ(status.Code(), core::ErrorCode::ParseError);
  DHEPZ_CHECK(back.empty());
  RemoveTestDir();
}

DHEPZ_TEST(Files, ReadMissingFileIsNotFound) {
  const std::wstring dir = MakeTestDir();
  std::wstring back;
  const core::Status status = files::ReadText(dir + L"\\nope.txt", &back);
  DHEPZ_CHECK_FALSE(status.ok());
  DHEPZ_CHECK_EQ(status.Code(), core::ErrorCode::NotFound);
  RemoveTestDir();
}

// An unpaired surrogate has no UTF-8 encoding and a std::wstring can hold
// one, so a write path must reject it rather than write a mangled file.
DHEPZ_TEST(Files, WriteRejectsUnpairedSurrogate) {
  const std::wstring dir = MakeTestDir();
  const std::wstring broken(1, static_cast<wchar_t>(0xD800));
  const core::Status status = files::WriteTextAtomic(dir + L"\\x.txt", broken);
  DHEPZ_CHECK_FALSE(status.ok());
  DHEPZ_CHECK_EQ(status.Code(), core::ErrorCode::ParseError);
  DHEPZ_CHECK(!std::filesystem::exists(dir + L"\\x.txt"));
  DHEPZ_CHECK(!std::filesystem::exists(dir + L"\\x.txt.tmp"));
  RemoveTestDir();
}

DHEPZ_TEST(Files, BackupAndRestoreRoundTrip) {
  const std::wstring dir = MakeTestDir();
  const std::wstring path = dir + L"\\ui.json";

  // No file yet: backing up is a no-op, not an error.
  DHEPZ_CHECK(files::MakeBackup(path).ok());
  DHEPZ_CHECK_FALSE(files::HasBackup(path));

  DHEPZ_CHECK(files::WriteTextAtomic(path, L"version one").ok());
  DHEPZ_CHECK(files::MakeBackup(path).ok());
  DHEPZ_CHECK(files::HasBackup(path));
  DHEPZ_CHECK_EQ(files::BackupPath(path), path + L".otn.bak");

  // A destructive change, then the restore brings the backup back.
  DHEPZ_CHECK(files::WriteTextInPlace(path, L"version two, corrupted beyond repair").ok());
  DHEPZ_CHECK(files::RestoreBackup(path).ok());
  std::wstring back;
  DHEPZ_CHECK(files::ReadText(path, &back).ok());
  DHEPZ_CHECK_EQ(back, std::wstring(L"version one"));

  RemoveTestDir();
}

DHEPZ_TEST(Files, RestoreWithoutBackupIsNotFound) {
  const std::wstring dir = MakeTestDir();
  const core::Status status = files::RestoreBackup(dir + L"\\never.txt");
  DHEPZ_CHECK_FALSE(status.ok());
  DHEPZ_CHECK_EQ(status.Code(), core::ErrorCode::NotFound);
  RemoveTestDir();
}
