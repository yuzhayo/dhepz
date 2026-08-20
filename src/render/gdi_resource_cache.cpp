#include "render/gdi_resource_cache.h"

#include "platform/performance_trace.h"

namespace render {

GdiResourceCache::~GdiResourceCache() {
  for (const auto& entry : fonts_) {
    DeleteObject(entry.second.font);
  }
  fonts_.clear();
  images_.clear();
}

HFONT GdiResourceCache::Font(const FontSpec& spec) {
  if (const auto found = fonts_.find(spec); found != fonts_.end()) {
    return found->second.font;
  }
  if (fonts_.size() >= kFontBound) {
    EvictOldestFont();
  }
  const HFONT font =
      CreateFontW(-spec.height_px, 0, 0, 0, spec.weight, spec.italic ? TRUE : FALSE, FALSE,
                  FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                  CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, spec.family.c_str());
  if (font == nullptr) {
    return nullptr;
  }
  fonts_[spec] = FontEntry{font, next_order_++};
  return font;
}

HFONT GdiResourceCache::FindFont(const FontSpec& spec) const {
  const auto found = fonts_.find(spec);
  return found != fonts_.end() ? found->second.font : nullptr;
}

std::uint64_t GdiResourceCache::StoreImage(ImageData image) {
  if (images_.size() >= kImageBound) {
    images_.erase(images_.begin());  // std::map orders by id: begin is oldest
  }
  const std::uint64_t id = next_image_id_++;
  images_.emplace(id, std::move(image));
  return id;
}

const ImageData* GdiResourceCache::Image(std::uint64_t id) const {
  const auto found = images_.find(id);
  return found != images_.end() ? &found->second : nullptr;
}

void GdiResourceCache::ReleaseImage(std::uint64_t id) { images_.erase(id); }

void GdiResourceCache::AdvanceEpoch() {
  ++epoch_;
  // One operation, not a hunt: the whole theme layer goes.
  for (const auto& entry : fonts_) {
    DeleteObject(entry.second.font);
  }
  fonts_.clear();
}

CacheSizes GdiResourceCache::Sizes() const noexcept {
  return CacheSizes{fonts_.size(), kFontBound, images_.size(), kImageBound};
}

void GdiResourceCache::FillSnapshot(trace::ResourceSnapshot& snapshot) const {
  const CacheSizes sizes = Sizes();
  if (snapshot.cache_count < trace::ResourceSnapshot::kMaxCaches) {
    snapshot.caches[snapshot.cache_count++] = {L"gdi.fonts", sizes.fonts};
  }
  if (snapshot.cache_count < trace::ResourceSnapshot::kMaxCaches) {
    snapshot.caches[snapshot.cache_count++] = {L"gdi.images", sizes.images};
  }
}

void GdiResourceCache::EvictOldestFont() {
  auto oldest = fonts_.end();
  for (auto it = fonts_.begin(); it != fonts_.end(); ++it) {
    if (oldest == fonts_.end() || it->second.order < oldest->second.order) {
      oldest = it;
    }
  }
  if (oldest != fonts_.end()) {
    DeleteObject(oldest->second.font);
    fonts_.erase(oldest);
  }
}

}  // namespace render
