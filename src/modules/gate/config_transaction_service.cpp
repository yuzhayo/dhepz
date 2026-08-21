#include "modules/gate/config_transaction_service.h"

#include <algorithm>
#include <set>
#include <utility>

#include "ui/config/config_store.h"

namespace modules {
namespace {

bool SameNode(const ui::config::ComponentNode& left,
              const ui::config::ComponentNode& right) {
  if (left.type_ != right.type_ || left.id_ != right.id_ ||
      left.properties_.size() != right.properties_.size() ||
      left.children_.size() != right.children_.size()) {
    return false;
  }
  for (std::size_t i = 0; i < left.properties_.size(); ++i) {
    if (left.properties_[i].first != right.properties_[i].first ||
        json::Serialize(left.properties_[i].second) !=
            json::Serialize(right.properties_[i].second)) {
      return false;
    }
  }
  for (std::size_t i = 0; i < left.children_.size(); ++i) {
    if (!SameNode(left.children_[i], right.children_[i])) return false;
  }
  return true;
}

bool SameRoute(const ui::config::Route& left,
               const ui::config::Route& right) {
  return left.id == right.id && left.tab_label == right.tab_label &&
         left.show_in_tabs == right.show_in_tabs &&
         left.backdrop_kind == right.backdrop_kind &&
         left.backdrop_value == right.backdrop_value &&
         SameNode(left.root, right.root);
}

std::vector<std::wstring> ChangedRoutes(
    const ui::config::ResolvedUiDocument& before,
    const ui::config::ResolvedUiDocument& after) {
  std::set<std::wstring> ids;
  for (const ui::config::Route& route : before.routes()) ids.insert(route.id);
  for (const ui::config::Route& route : after.routes()) ids.insert(route.id);
  std::vector<std::wstring> changed;
  for (const std::wstring& id : ids) {
    const ui::config::Route* old_route = before.FindRoute(id);
    const ui::config::Route* new_route = after.FindRoute(id);
    if (old_route == nullptr || new_route == nullptr ||
        !SameRoute(*old_route, *new_route)) {
      changed.push_back(id);
    }
  }
  return changed;
}

}  // namespace

ConfigTransactionService::ConfigTransactionService(
    std::unique_ptr<ui::config::ResolvedUiDocument>* live_document,
    std::uint64_t* document_generation)
    : live_document_(live_document),
      document_generation_(document_generation) {}

void ConfigTransactionService::ConfigureBase(
    json::Value core_catalog, ui::config::ScreenSource embedded_source) {
  core_catalog_ = std::move(core_catalog);
  embedded_source_ = std::move(embedded_source);
  previous_document_.reset();
  candidate_.clear();
  active_preview_ = {};
  save_pending_ = false;
}

core::Status ConfigTransactionService::ConfigureOverridePath(std::wstring path) {
  if (active_preview_ || save_pending_) {
    return core::Err(core::ErrorCode::AlreadyExists,
                     L"config transaction is active");
  }
  override_path_ = std::move(path);
  return core::Ok();
}

core::Status ConfigTransactionService::Preview(std::wstring_view candidate,
                                               ConfigPreviewResult* result) {
  if (result == nullptr) {
    return core::Err(core::ErrorCode::InvalidArgument,
                     L"preview result is required");
  }
  *result = {};
  if (live_document_ == nullptr || !*live_document_) {
    return core::Err(core::ErrorCode::InvalidArgument,
                     L"gate has no resolved document");
  }
  if (save_pending_) {
    return core::Err(core::ErrorCode::AlreadyExists,
                     L"config save is pending");
  }

  const std::wstring source_name =
      override_path_.empty() ? L"config preview" : override_path_;
  std::vector<ui::config::Diagnostic> diagnostics;
  std::unique_ptr<ui::config::ResolvedUiDocument> next;
  const core::Status resolved = ui::config::ResolveDocument(
      core_catalog_,
      {embedded_source_, {source_name, std::wstring(candidate)}},
      &diagnostics, &next);
  if (!resolved.ok()) {
    for (const ui::config::Diagnostic& diagnostic : diagnostics) {
      result->diagnostics.push_back(
          {source_name, diagnostic.line, diagnostic.column,
           diagnostic.message});
    }
    return resolved;
  }

  result->affected_routes = ChangedRoutes(**live_document_, *next);
  if (!previous_document_) {
    previous_document_ = std::move(*live_document_);
  }
  *live_document_ = std::move(next);
  candidate_ = std::wstring(candidate);
  active_preview_.value = next_preview_token_++;
  result->token = active_preview_;
  ++*document_generation_;
  return core::Ok();
}

core::Status ConfigTransactionService::BeginSave(ConfigPreviewToken preview,
                                                  std::wstring* path,
                                                  std::wstring* text) {
  if (path == nullptr || text == nullptr) {
    return core::Err(core::ErrorCode::InvalidArgument,
                     L"save outputs are required");
  }
  if (!active_preview_ || preview != active_preview_ || !previous_document_) {
    return core::Err(core::ErrorCode::InvalidArgument,
                     L"stale config preview token");
  }
  if (save_pending_) {
    return core::Err(core::ErrorCode::AlreadyExists,
                     L"config save is pending");
  }
  if (override_path_.empty()) {
    return core::Err(core::ErrorCode::InvalidArgument,
                     L"config override path is not configured");
  }
  save_pending_ = true;
  *path = override_path_;
  *text = candidate_;
  return core::Ok();
}

void ConfigTransactionService::CompleteSave(ConfigPreviewToken preview,
                                            const core::Status& status) {
  if (preview != active_preview_) return;
  save_pending_ = false;
  if (!status.ok()) return;
  previous_document_.reset();
  candidate_.clear();
  active_preview_ = {};
}

core::Status ConfigTransactionService::AbortSave(ConfigPreviewToken preview) {
  if (preview != active_preview_) {
    return core::Err(core::ErrorCode::InvalidArgument,
                     L"stale config preview token");
  }
  save_pending_ = false;
  return core::Ok();
}

core::Status ConfigTransactionService::Discard(
    ConfigPreviewToken preview,
    std::vector<std::wstring>* affected_routes) {
  if (affected_routes == nullptr) {
    return core::Err(core::ErrorCode::InvalidArgument,
                     L"affected-routes output is required");
  }
  affected_routes->clear();
  if (!active_preview_ || preview != active_preview_ || !previous_document_) {
    return core::Err(core::ErrorCode::InvalidArgument,
                     L"stale config preview token");
  }
  if (save_pending_) {
    return core::Err(core::ErrorCode::AlreadyExists,
                     L"config save is pending");
  }
  *affected_routes = ChangedRoutes(**live_document_, *previous_document_);
  *live_document_ = std::move(previous_document_);
  candidate_.clear();
  active_preview_ = {};
  ++*document_generation_;
  return core::Ok();
}

}  // namespace modules
