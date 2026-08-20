// Two-tier config resolution with corrupt-override quarantine (#58).
//
//   bootstrap:  corrupt override -> embedded config used, diagnostic
//               published, corrupt file renamed aside (.quarantine)
//   hot reload: corrupt override -> the live document stays active and a
//               diagnostic is recorded; nothing swaps, nothing crashes
//   recovery:   a valid override placed at the path is picked up on the
//               next reload
//
// Truncated or partial writes are just parse errors to this reader, so a
// half-written override can never surface as a half-resolved document;
// writers (the future ui-editor) use files::WriteTextAtomic.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/status.h"
#include "ui/config/resolved_ui_document.h"

namespace ui::config {

class ConfigStore final {
 public:
  // `core_text` and `embedded` are the shipped tier; `override_path` is the
  // user tier (one screens document).
  ConfigStore(std::wstring core_text, std::vector<ScreenSource> embedded,
              std::wstring override_path);

  // Bootstrap. Never fails the process: a corrupt override degrades to the
  // embedded tier and records diagnostics.
  core::Status Load();

  // Hot reload. A corrupt or unresolvable override keeps the live document
  // active and records a diagnostic; a valid one swaps atomically.
  core::Status Reload();

  const ResolvedUiDocument* document() const { return document_.get(); }
  // Cumulative diagnostics, oldest first; source names identify the tier.
  const std::vector<Diagnostic>& diagnostics() const { return diagnostics_; }

 private:
  bool ReadOverrideText(std::wstring* text);
  void QuarantineOverride();
  core::Status ResolveWith(const std::vector<ScreenSource>& sources,
                           std::unique_ptr<ResolvedUiDocument>* out,
                           std::vector<Diagnostic>* diags);

  std::wstring core_text_;
  std::vector<ScreenSource> embedded_;
  std::wstring override_path_;
  std::unique_ptr<ResolvedUiDocument> document_;
  std::vector<Diagnostic> diagnostics_;
};

}  // namespace ui::config
