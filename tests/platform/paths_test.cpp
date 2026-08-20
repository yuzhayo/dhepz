#include "platform/paths.h"

#include <windows.h>

#include <string>

#include "framework/test_case.h"

namespace {

// A scratch directory under %TEMP% that no other process is going to touch.
// The PID keeps parallel or leftover runs from colliding.
std::wstring TestRoot() {
  wchar_t buffer[MAX_PATH] = {};
  const DWORD len = GetTempPathW(MAX_PATH, buffer);
  std::wstring root(buffer, len);
  root += L"dhepz_paths_test_";
  root += std::to_wstring(GetCurrentProcessId());
  return root;
}

}  // namespace

DHEPZ_TEST(Normalize, ResolvesMixedSeparatorsAndDotSegments) {
  DHEPZ_CHECK_EQ(paths::Normalize(L"C:\\a/./b/../c"), std::wstring(L"C:\\a\\c"));
  DHEPZ_CHECK_EQ(paths::Normalize(L"C:\\a\\.\\b"), std::wstring(L"C:\\a\\b"));
  DHEPZ_CHECK_EQ(paths::Normalize(L"C:\\a\\b\\.."), std::wstring(L"C:\\a"));
}

DHEPZ_TEST(Normalize, StripsTrailingSeparators) {
  DHEPZ_CHECK_EQ(paths::Normalize(L"C:\\a\\b\\\\"), std::wstring(L"C:\\a\\b"));
  DHEPZ_CHECK_EQ(paths::Normalize(L"C:\\a\\b//"), std::wstring(L"C:\\a\\b"));
}

// The drive root is the one path whose trailing separator must survive —
// "C:" without it means something entirely different (CWD on drive C).
DHEPZ_TEST(Normalize, DriveRootKeepsItsSeparator) {
  DHEPZ_CHECK_EQ(paths::Normalize(L"C:\\"), std::wstring(L"C:\\"));
  DHEPZ_CHECK_EQ(paths::Normalize(L"C:/"), std::wstring(L"C:\\"));
}

DHEPZ_TEST(Normalize, UncPathSurvives) {
  DHEPZ_CHECK_EQ(paths::Normalize(L"\\\\server\\share\\dir\\..\\x\\"),
                 std::wstring(L"\\\\server\\share\\x"));
}

DHEPZ_TEST(Normalize, ExpandsEnvironmentVariables) {
  wchar_t buffer[MAX_PATH] = {};
  const DWORD len = GetTempPathW(MAX_PATH, buffer);
  DHEPZ_CHECK(len > 0);
  const std::wstring expected = paths::Normalize(std::wstring(buffer, len) + L"sub");
  DHEPZ_CHECK(!expected.empty());
  DHEPZ_CHECK_EQ(paths::Normalize(L"%TEMP%\\sub"), expected);
}

DHEPZ_TEST(Normalize, TrimsWhitespaceFirst) {
  DHEPZ_CHECK_EQ(paths::Normalize(L"  C:\\a\\b  "), std::wstring(L"C:\\a\\b"));
}

DHEPZ_TEST(Normalize, PreservesNameCase) {
  DHEPZ_CHECK_EQ(paths::Normalize(L"C:\\Folder\\NAME"), std::wstring(L"C:\\Folder\\NAME"));
}

// Resolving a relative path would silently depend on the process CWD, which
// is exactly the hidden dependency this layer refuses to introduce.
DHEPZ_TEST(Normalize, RejectsRelativeInput) {
  DHEPZ_CHECK(paths::Normalize(L"relative\\dir").empty());
  DHEPZ_CHECK(paths::Normalize(L".\\here").empty());
  DHEPZ_CHECK(paths::Normalize(L"..\\up").empty());
  // Rooted on the current drive but still CWD-dependent.
  DHEPZ_CHECK(paths::Normalize(L"\\rooted\\no\\drive").empty());
}

DHEPZ_TEST(Normalize, EmptyInputIsEmpty) {
  DHEPZ_CHECK(paths::Normalize(L"").empty());
  DHEPZ_CHECK(paths::Normalize(L"   ").empty());
}

// The manifest declares longPathAware, so a path past MAX_PATH must survive
// normalisation rather than being truncated or failing.
DHEPZ_TEST(Normalize, HandlesLongPaths) {
  std::wstring long_path = L"C:\\base";
  for (int i = 0; i < 12; ++i) {
    long_path += L"\\segment-of-about-thirty-chars";
  }
  DHEPZ_CHECK(long_path.size() > MAX_PATH);
  DHEPZ_CHECK_EQ(paths::Normalize(long_path + L"\\"), long_path);
}

DHEPZ_TEST(ValidateUnderRoot, AcceptsPathEqualOrInside) {
  std::wstring full;
  DHEPZ_CHECK(paths::ValidateUnderRoot(L"C:\\Root", L"C:\\Root", &full).ok());
  DHEPZ_CHECK_EQ(full, std::wstring(L"C:\\Root"));

  DHEPZ_CHECK(paths::ValidateUnderRoot(L"C:\\Root", L"C:\\Root\\a\\b", &full).ok());
  DHEPZ_CHECK_EQ(full, std::wstring(L"C:\\Root\\a\\b"));
}

// NTFS is case-insensitive, so the root itself arriving in a different case
// is the same place — not an escape.
DHEPZ_TEST(ValidateUnderRoot, IsCaseInsensitive) {
  std::wstring full;
  DHEPZ_CHECK(paths::ValidateUnderRoot(L"C:\\Root", L"c:\\root\\child", &full).ok());
  DHEPZ_CHECK_EQ(full, std::wstring(L"c:\\root\\child"));
}

DHEPZ_TEST(ValidateUnderRoot, RejectsDotDotEscape) {
  std::wstring full;
  const core::Status status = paths::ValidateUnderRoot(L"C:\\Root", L"C:\\Root\\..\\other", &full);
  DHEPZ_CHECK_FALSE(status.ok());
  DHEPZ_CHECK_EQ(status.Code(), core::ErrorCode::PermissionDenied);
  DHEPZ_CHECK(full.empty());
}

DHEPZ_TEST(ValidateUnderRoot, RejectsSiblingAndPrefixLookalike) {
  std::wstring full;
  // A sibling is outside regardless of how the path is written.
  DHEPZ_CHECK_FALSE(paths::ValidateUnderRoot(L"C:\\Root", L"C:\\Elsewhere", &full).ok());
  // "RootExtra" starts with the root's *text* but is not under it — the
  // separator boundary is what decides.
  DHEPZ_CHECK_FALSE(paths::ValidateUnderRoot(L"C:\\Root", L"C:\\RootExtra\\x", &full).ok());
}

DHEPZ_TEST(ValidateUnderRoot, RejectsDifferentDrive) {
  std::wstring full;
  DHEPZ_CHECK_FALSE(paths::ValidateUnderRoot(L"C:\\Root", L"D:\\Root\\a", &full).ok());
}

DHEPZ_TEST(ValidateUnderRoot, ResolvesBeforeComparing) {
  std::wstring full;
  // The escape only becomes visible after `.` and `..` are resolved, and the
  // %VAR% form must reach the same verdict as the literal one.
  DHEPZ_CHECK(paths::ValidateUnderRoot(L"%TEMP%", L"%TEMP%\\.\\sub\\..\\sub", &full).ok());
  DHEPZ_CHECK_EQ(full, paths::Normalize(L"%TEMP%\\sub"));
  DHEPZ_CHECK_FALSE(paths::ValidateUnderRoot(L"%TEMP%", L"%TEMP%\\..\\x", &full).ok());
}

DHEPZ_TEST(ValidateUnderRoot, WorksForUncRoots) {
  std::wstring full;
  DHEPZ_CHECK(paths::ValidateUnderRoot(L"\\\\server\\share", L"\\\\server\\share\\dir", &full).ok());
  DHEPZ_CHECK_FALSE(
      paths::ValidateUnderRoot(L"\\\\server\\share", L"\\\\server\\othershare", &full).ok());
}

DHEPZ_TEST(ValidateUnderRoot, RejectsBadArguments) {
  std::wstring full;
  DHEPZ_CHECK_EQ(paths::ValidateUnderRoot(L"", L"C:\\a", &full).Code(),
                 core::ErrorCode::InvalidArgument);
  DHEPZ_CHECK_EQ(paths::ValidateUnderRoot(L"C:\\Root", L"relative", &full).Code(),
                 core::ErrorCode::InvalidArgument);
  DHEPZ_CHECK_EQ(paths::ValidateUnderRoot(L"C:\\Root", L"C:\\a", nullptr).Code(),
                 core::ErrorCode::InvalidArgument);
}

DHEPZ_TEST(StateDir, ShapeAndLazyCreation) {
  const std::wstring dir = paths::StateDir();
  const std::wstring suffix = L"\\dhepz\\state";
  DHEPZ_CHECK(dir.size() > suffix.size());
  DHEPZ_CHECK_EQ(std::wstring_view(dir).substr(dir.size() - suffix.size()), suffix);
  DHEPZ_CHECK_FALSE(dir.back() == L'\\');

  // The pure call must not have created anything: if nothing exists yet, it
  // must still not exist after asking for the path. (A directory left over
  // from an earlier run of the app skips this half — the soak criterion in
  // the issue is the authoritative check.)
  const std::wstring parent = paths::Join(paths::LocalAppDataDir(), L"dhepz");
  if (!paths::DirectoryExists(parent)) {
    DHEPZ_CHECK_FALSE(paths::DirectoryExists(dir));
  }
}

DHEPZ_TEST(StateDir, EnsureCreatesThenIsIdempotent) {
  DHEPZ_CHECK(paths::EnsureStateDir().ok());
  DHEPZ_CHECK(paths::DirectoryExists(paths::StateDir()));
  // A second call against an existing directory is not an error.
  DHEPZ_CHECK(paths::EnsureStateDir().ok());
}

DHEPZ_TEST(Paths, JoinParentFileName) {
  DHEPZ_CHECK_EQ(paths::Join(L"C:\\a", L"b"), std::wstring(L"C:\\a\\b"));
  DHEPZ_CHECK_EQ(paths::Join(L"C:\\a\\", L"b"), std::wstring(L"C:\\a\\b"));
  DHEPZ_CHECK_EQ(paths::Join(L"C:\\a", L"\\b"), std::wstring(L"C:\\a\\b"));
  DHEPZ_CHECK_EQ(paths::Join(L"C:\\a", L""), std::wstring(L"C:\\a"));
  DHEPZ_CHECK_EQ(paths::Join(L"", L"b"), std::wstring(L"b"));

  DHEPZ_CHECK_EQ(paths::Parent(L"C:\\a\\b.txt"), std::wstring(L"C:\\a"));
  DHEPZ_CHECK_EQ(paths::Parent(L"no_separator"), std::wstring(L""));
  DHEPZ_CHECK_EQ(paths::FileName(L"C:\\a\\b.txt"), std::wstring(L"b.txt"));
  DHEPZ_CHECK_EQ(paths::FileName(L"bare"), std::wstring(L"bare"));
}

DHEPZ_TEST(Paths, ExistsAndEnsureDirectory) {
  const std::wstring root = TestRoot();
  const std::wstring deep = paths::Join(root, L"x\\y\\z");
  DHEPZ_CHECK_FALSE(paths::DirectoryExists(deep));

  DHEPZ_CHECK(paths::EnsureDirectory(deep));
  DHEPZ_CHECK(paths::DirectoryExists(deep));
  DHEPZ_CHECK(paths::EnsureDirectory(deep));  // idempotent

  const std::wstring file = paths::Join(deep, L"f.txt");
  DHEPZ_CHECK_FALSE(paths::FileExists(file));
  HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  DHEPZ_CHECK(h != INVALID_HANDLE_VALUE);
  if (h != INVALID_HANDLE_VALUE) {
    CloseHandle(h);
  }
  DHEPZ_CHECK(paths::FileExists(file));
  // A file is not a directory and vice versa.
  DHEPZ_CHECK_FALSE(paths::DirectoryExists(file));
  DHEPZ_CHECK_FALSE(paths::FileExists(deep));

  DeleteFileW(file.c_str());
  RemoveDirectoryW(paths::Join(root, L"x\\y\\z").c_str());
  RemoveDirectoryW(paths::Join(root, L"x\\y").c_str());
  RemoveDirectoryW(paths::Join(root, L"x").c_str());
  RemoveDirectoryW(root.c_str());
}
