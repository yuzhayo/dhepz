#include "ui/config/config_store.h"

#include <windows.h>

#include "platform/files.h"

namespace ui::config {

ConfigStore::ConfigStore(std::wstring core_text, std::vector<ScreenSource> embedded,
                         std::wstring override_path)
    : core_text_(std::move(core_text)),
      embedded_(std::move(embedded)),
      override_path_(std::move(override_path)) {}

bool ConfigStore::ReadOverrideText(std::wstring* text) {
  return files::ReadText(override_path_, text).ok();
}

void ConfigStore::QuarantineOverride() {
  // NTFS rename is atomic: a reader can never observe a half-moved file.
  MoveFileExW(override_path_.c_str(), (override_path_ + L".quarantine").c_str(),
              MOVEFILE_REPLACE_EXISTING);
}

core::Status ConfigStore::ResolveWith(const std::vector<ScreenSource>& sources,
                                      std::unique_ptr<ResolvedUiDocument>* out,
                                      std::vector<Diagnostic>* diags) {
  json::Value core;
  const core::Status parsed = json::Parse(core_text_, &core);
  if (!parsed.ok()) {
    diags->push_back({L"core: " + parsed.Message(), 0, 0});
    return parsed;
  }
  return ResolveDocument(core, sources, diags, out);
}

core::Status ConfigStore::Load() {
  std::vector<ScreenSource> sources = embedded_;
  std::wstring override_text;
  if (ReadOverrideText(&override_text)) {
    sources.push_back({L"override", override_text});
    std::vector<Diagnostic> attempt;
    std::unique_ptr<ResolvedUiDocument> merged;
    if (ResolveWith(sources, &merged, &attempt).ok()) {
      document_ = std::move(merged);
      diagnostics_.insert(diagnostics_.end(), attempt.begin(), attempt.end());
      return core::Ok();
    }
    // Corrupt or invalid override at bootstrap: fall back to embedded and
    // move the file aside so the next start begins clean.
    diagnostics_.insert(diagnostics_.end(), attempt.begin(), attempt.end());
    diagnostics_.push_back(
        {L"override: corrupt override quarantined; embedded config in use", 0, 0});
    QuarantineOverride();
  }
  std::vector<Diagnostic> embedded_diags;
  const core::Status status = ResolveWith(embedded_, &document_, &embedded_diags);
  diagnostics_.insert(diagnostics_.end(), embedded_diags.begin(), embedded_diags.end());
  return status;
}

core::Status ConfigStore::Reload() {
  std::wstring override_text;
  std::vector<ScreenSource> sources = embedded_;
  if (ReadOverrideText(&override_text)) {
    sources.push_back({L"override", override_text});
  }
  std::vector<Diagnostic> attempt;
  std::unique_ptr<ResolvedUiDocument> next;
  const core::Status status = ResolveWith(sources, &next, &attempt);
  if (!status.ok()) {
    // Hot reload never takes the app down: the live document stays active.
    diagnostics_.insert(diagnostics_.end(), attempt.begin(), attempt.end());
    return status;
  }
  document_ = std::move(next);
  return core::Ok();
}

}  // namespace ui::config
