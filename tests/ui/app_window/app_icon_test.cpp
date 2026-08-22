#include <windows.h>

#include "framework/test_case.h"
#include "resource.h"

namespace {

#pragma pack(push, 2)
struct GroupIconDir {
  WORD reserved;
  WORD type;
  WORD count;
};
struct GroupIconEntry {
  BYTE width;
  BYTE height;
  BYTE colors;
  BYTE reserved;
  WORD planes;
  WORD bitcount;
  DWORD bytes;
  WORD id;
};
#pragma pack(pop)

bool HasSize(const GroupIconEntry* entries, WORD count, int size) {
  const BYTE want = size >= 256 ? 0 : static_cast<BYTE>(size);
  for (WORD i = 0; i < count; ++i) {
    if (entries[i].width == want && entries[i].height == want) return true;
  }
  return false;
}

}  // namespace

DHEPZ_TEST(AppIcon, EmbeddedIconCarriesTheExpectedSizes) {
  const HMODULE module = GetModuleHandleW(nullptr);
  const HRSRC resource =
      FindResourceW(module, MAKEINTRESOURCEW(IDI_APP_ICON), RT_GROUP_ICON);
  DHEPZ_CHECK(resource != nullptr);
  const HGLOBAL data = LoadResource(module, resource);
  DHEPZ_CHECK(data != nullptr);
  const auto* dir = static_cast<const GroupIconDir*>(LockResource(data));
  DHEPZ_CHECK(dir != nullptr);
  DHEPZ_CHECK_EQ(static_cast<unsigned long long>(dir->type), 1ull);

  const auto* entries = reinterpret_cast<const GroupIconEntry*>(dir + 1);
  DHEPZ_CHECK(dir->count >= 4);
  DHEPZ_CHECK(HasSize(entries, dir->count, 16));
  DHEPZ_CHECK(HasSize(entries, dir->count, 32));
  DHEPZ_CHECK(HasSize(entries, dir->count, 48));
  DHEPZ_CHECK(HasSize(entries, dir->count, 256));
}
