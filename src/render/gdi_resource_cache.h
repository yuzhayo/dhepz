// The bounded, epoch-keyed resource cache behind the GDI backend.
//
// Fonts, images and back buffers are expensive to create and cheap to keep,
// and the plan is explicit that they persist across window close: dropping
// them means re-rendering every rounded corner on the next show, which is
// exactly the latency the cache exists to remove (G2). The catch is that
// anything kept must be BOUNDED, or it becomes drift (G1). Both at once:
//
//   - Three layers, as in the old build.
//       Process : decoded images — survive every window, released by ID.
//       Theme   : fonts — invalidated in one operation by an epoch bump.
//       Window  : back buffers — owned by each GdiBackend, released when
//                 the backend (its window) is destroyed. This layer lives
//                 in the backend itself; the cache covers the shared two.
//   - Every layer has a hard entry bound. Overflow evicts the oldest entry;
//     an unbounded cache is a leak with good manners.
//   - A resource epoch bumps on theme change or DPI change. The theme
//     layer is discarded wholesale on the bump — one operation, not a hunt
//     through individual entries.
//
// Sizes are reported through FillSnapshot so drift is visible in
// measurement rather than discovered at hour six.
//
// This is an internal header of the GDI implementation: it carries Windows
// types on purpose. The public seam (render_backend.h) stays clean.
#pragma once

#include <windows.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace trace {
struct ResourceSnapshot;
}

namespace render {

struct FontSpec {
  std::wstring family;
  int height_px = 0;
  int weight = FW_NORMAL;
  bool italic = false;

  bool operator<(const FontSpec& other) const noexcept {
    if (family != other.family) return family < other.family;
    if (height_px != other.height_px) return height_px < other.height_px;
    if (weight != other.weight) return weight < other.weight;
    return italic < other.italic;
  }
};

// Decoded image pixels, premultiplied, packed (a<<24)|(r<<16)|(g<<8)|b —
// the same layout as the back buffer.
struct ImageData {
  int width = 0;
  int height = 0;
  std::vector<std::uint32_t> pixels;
};

struct CacheSizes {
  std::size_t fonts = 0;
  std::size_t font_bound = 0;
  std::size_t images = 0;
  std::size_t image_bound = 0;
};

class GdiResourceCache final {
 public:
  static constexpr std::size_t kFontBound = 128;
  static constexpr std::size_t kImageBound = 64;

  GdiResourceCache() = default;
  ~GdiResourceCache();

  GdiResourceCache(const GdiResourceCache&) = delete;
  GdiResourceCache& operator=(const GdiResourceCache&) = delete;

  // Theme layer. Returns a cached font or creates it. nullptr only on real
  // creation failure. Entries from a previous epoch do not survive the bump.
  HFONT Font(const FontSpec& spec);
  // Lookup only: what a paint scope may use (warmed fonts), never create.
  HFONT FindFont(const FontSpec& spec) const;

  // Process layer. Stores decoded pixels and returns a nonzero id. When the
  // bound is reached the oldest image is evicted first; a store never fails
  // on capacity.
  std::uint64_t StoreImage(ImageData image);
  const ImageData* Image(std::uint64_t id) const;
  void ReleaseImage(std::uint64_t id);

  std::uint64_t epoch() const noexcept { return epoch_; }
  // Theme change or DPI change: discard the theme layer in one operation.
  void AdvanceEpoch();

  CacheSizes Sizes() const noexcept;
  // Copies the counters into the first free snapshot slots, so leak tests
  // and the ETW ResourceSnapshot see the caches without knowing about them.
  void FillSnapshot(trace::ResourceSnapshot& snapshot) const;

 private:
  struct FontEntry {
    HFONT font = nullptr;
    std::uint64_t order = 0;
  };

  void EvictOldestFont();

  std::map<FontSpec, FontEntry> fonts_;
  std::map<std::uint64_t, ImageData> images_;
  std::uint64_t epoch_ = 1;
  std::uint64_t next_order_ = 1;
  std::uint64_t next_image_id_ = 1;
};

}  // namespace render
