// Parent-owned preview/save/discard transaction for config:write. AppGate owns
// this service but does not implement its validation or revision state.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/json.h"
#include "modules/contract/module_contract.h"
#include "ui/config/resolved_ui_document.h"

namespace modules {

class ConfigTransactionService final {
 public:
  ConfigTransactionService(
      std::unique_ptr<ui::config::ResolvedUiDocument>* live_document,
      std::uint64_t* document_generation);

  void ConfigureBase(json::Value core_catalog,
                     ui::config::ScreenSource embedded_source);
  core::Status ConfigureOverridePath(std::wstring path);
  core::Status Preview(std::wstring_view candidate,
                       ConfigPreviewResult* result);
  core::Status BeginSave(ConfigPreviewToken preview, std::wstring* path,
                         std::wstring* text);
  void CompleteSave(ConfigPreviewToken preview, const core::Status& status);
  core::Status AbortSave(ConfigPreviewToken preview);
  core::Status Discard(ConfigPreviewToken preview,
                       std::vector<std::wstring>* affected_routes);

 private:
  std::unique_ptr<ui::config::ResolvedUiDocument>* live_document_;
  std::uint64_t* document_generation_;
  json::Value core_catalog_;
  ui::config::ScreenSource embedded_source_;
  std::unique_ptr<ui::config::ResolvedUiDocument> previous_document_;
  std::wstring candidate_;
  ConfigPreviewToken active_preview_;
  std::uint64_t next_preview_token_ = 1;
  bool save_pending_ = false;
  std::wstring override_path_;
};

}  // namespace modules
