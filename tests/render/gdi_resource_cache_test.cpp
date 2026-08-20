#include "render/gdi_resource_cache.h"

#include <windows.h>
#include <psapi.h>

#include <cstdint>
#include <string>

#include "framework/test_case.h"
#include "platform/performance_trace.h"
#include "render/gdi_backend.h"

namespace {

DWORD GdiObjects() {
  return GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
}

render::FontSpec SpecAt(int height_px) {
  return render::FontSpec{L"Segoe UI", height_px, FW_NORMAL, false};
}

render::ImageData MakeImage(int width, int height, std::uint32_t pixel) {
  render::ImageData image;
  image.width = width;
  image.height = height;
  image.pixels.assign(static_cast<std::size_t>(width) * height, pixel);
  return image;
}

}  // namespace

DHEPZ_TEST(GdiResourceCache, FontsPersistAcrossWindowClose) {
  render::GdiResourceCache cache;
  const DWORD before = GdiObjects();

  {
    render::GdiBackend window_a(&cache);
    window_a.Resize({100.0f, 40.0f});
    const render::TextStyle style;
    window_a.MeasureText(L"warm", style, 0.0f);
  }  // window closes: the window layer dies, the theme layer survives
  DHEPZ_CHECK_EQ(static_cast<unsigned long long>(cache.Sizes().fonts),
                 static_cast<unsigned long long>(1));

  {
    render::GdiBackend window_b(&cache);
    window_b.Resize({100.0f, 40.0f});
    const render::TextStyle style;
    window_b.MeasureText(L"warm", style, 0.0f);  // reuses, does not recreate
  }
  DHEPZ_CHECK_EQ(static_cast<unsigned long long>(cache.Sizes().fonts),
                 static_cast<unsigned long long>(1));
  // Exactly one font object exists; nothing leaked across the close.
  DHEPZ_CHECK_EQ(static_cast<unsigned long long>(GdiObjects()),
                 static_cast<unsigned long long>(before + 1));
}

DHEPZ_TEST(GdiResourceCache, FontLayerIsBounded) {
  render::GdiResourceCache cache;
  const DWORD before = GdiObjects();

  for (int height = 1; height <= 200; ++height) {
    DHEPZ_CHECK(cache.Font(SpecAt(height)) != nullptr);
  }
  const render::CacheSizes sizes = cache.Sizes();
  DHEPZ_CHECK(sizes.fonts <= render::GdiResourceCache::kFontBound);
  DHEPZ_CHECK_EQ(static_cast<unsigned long long>(sizes.font_bound),
                 static_cast<unsigned long long>(render::GdiResourceCache::kFontBound));
  // Handles track the cache, not the creation count: eviction released.
  DHEPZ_CHECK_EQ(static_cast<unsigned long long>(GdiObjects()),
                 static_cast<unsigned long long>(before + sizes.fonts));
}

DHEPZ_TEST(GdiResourceCache, EpochBumpDiscardsTheThemeLayerInOneOperation) {
  render::GdiResourceCache cache;
  const DWORD before = GdiObjects();

  cache.Font(SpecAt(12));
  cache.Font(SpecAt(14));
  DHEPZ_CHECK_EQ(static_cast<unsigned long long>(cache.Sizes().fonts),
                 static_cast<unsigned long long>(2));
  DHEPZ_CHECK_EQ(static_cast<unsigned long long>(GdiObjects()),
                 static_cast<unsigned long long>(before + 2));

  const std::uint64_t epoch = cache.epoch();
  cache.AdvanceEpoch();
  DHEPZ_CHECK(cache.epoch() == epoch + 1);
  DHEPZ_CHECK_EQ(static_cast<unsigned long long>(cache.Sizes().fonts),
                 static_cast<unsigned long long>(0));
  DHEPZ_CHECK_EQ(static_cast<unsigned long long>(GdiObjects()),
                 static_cast<unsigned long long>(before));

  // Lookup after a bump finds nothing; creation starts fresh.
  DHEPZ_CHECK(cache.FindFont(SpecAt(12)) == nullptr);
  DHEPZ_CHECK(cache.Font(SpecAt(12)) != nullptr);
}

DHEPZ_TEST(GdiResourceCache, FiftyThemeTogglesLeaveHandlesAndBytesFlat) {
  render::GdiResourceCache cache;
  render::GdiBackend backend(&cache);
  backend.Resize({120.0f, 40.0f});
  const render::TextStyle style;
  backend.MeasureText(L"warm", style, 0.0f);

  const DWORD handles_before = GdiObjects();
  const HANDLE process = GetCurrentProcess();
  PROCESS_MEMORY_COUNTERS memory_before{};
  memory_before.cb = sizeof(memory_before);
  K32GetProcessMemoryInfo(process, &memory_before, sizeof(memory_before));

  for (int i = 0; i < 50; ++i) {
    backend.MeasureText(L"theme toggle", style, 0.0f);
    cache.AdvanceEpoch();  // theme change: the whole layer goes at once
  }
  backend.MeasureText(L"theme toggle", style, 0.0f);

  PROCESS_MEMORY_COUNTERS memory_after{};
  memory_after.cb = sizeof(memory_after);
  K32GetProcessMemoryInfo(process, &memory_after, sizeof(memory_after));

  // One warmed font is alive at the end; nothing accumulated.
  DHEPZ_CHECK_EQ(static_cast<unsigned long long>(GdiObjects()),
                 static_cast<unsigned long long>(handles_before));
  const long long drift = static_cast<long long>(memory_after.WorkingSetSize) -
                          static_cast<long long>(memory_before.WorkingSetSize);
  DHEPZ_CHECK(drift < 1024 * 1024);
}

DHEPZ_TEST(GdiResourceCache, ImageLayerIsBoundedAndSurvivesWindows) {
  render::GdiResourceCache cache;

  std::uint64_t first_id = 0;
  {
    render::GdiBackend window_a(&cache);
    for (int i = 0; i < 70; ++i) {
      const std::uint64_t id = cache.StoreImage(MakeImage(2, 2, 0xFF112233));
      if (i == 69) first_id = id;
    }
  }
  // Bound holds after 70 stores; the newest survives.
  DHEPZ_CHECK_EQ(static_cast<unsigned long long>(cache.Sizes().images),
                 static_cast<unsigned long long>(render::GdiResourceCache::kImageBound));
  DHEPZ_CHECK(cache.Image(first_id) != nullptr);

  // A later window still sees the process-layer image.
  render::GdiBackend window_b(&cache);
  DHEPZ_CHECK(cache.Image(first_id) != nullptr);

  cache.ReleaseImage(first_id);
  DHEPZ_CHECK(cache.Image(first_id) == nullptr);
}

DHEPZ_TEST(GdiResourceCache, SizesAreReportedIntoTheResourceSnapshot) {
  render::GdiResourceCache cache;
  cache.Font(SpecAt(12));
  cache.StoreImage(MakeImage(1, 1, 0xFFFFFFFF));

  trace::ResourceSnapshot snapshot;
  cache.FillSnapshot(snapshot);
  DHEPZ_CHECK_EQ(static_cast<unsigned long long>(snapshot.cache_count),
                 static_cast<unsigned long long>(2));
  DHEPZ_CHECK_EQ(std::wstring(snapshot.caches[0].name), std::wstring(L"gdi.fonts"));
  DHEPZ_CHECK_EQ(static_cast<unsigned long long>(snapshot.caches[0].size),
                 static_cast<unsigned long long>(1));
  DHEPZ_CHECK_EQ(std::wstring(snapshot.caches[1].name), std::wstring(L"gdi.images"));
  DHEPZ_CHECK_EQ(static_cast<unsigned long long>(snapshot.caches[1].size),
                 static_cast<unsigned long long>(1));

  // And through the backend passthrough, as the shell will use it.
  render::GdiBackend backend(&cache);
  const render::CacheSizes sizes = backend.cache_sizes();
  DHEPZ_CHECK(sizes.fonts < sizes.font_bound);
  DHEPZ_CHECK(sizes.images < sizes.image_bound);
}
