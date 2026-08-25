#include "orchestrator/window_orchestrator.h"

#include <windows.h>

#include <algorithm>
#include <limits>

#include "core/status.h"
#include "orchestrator/module_host.h"
#include "parent/logic/module_contract.h"
#include "parent/logic/module_state_store.h"
#include "parent/ui/contracts/ui_action_registry.h"
#include "parent/ui/runtime/parent_ui.h"
#include "parent/ui/runtime/route_tabs.h"
#include "platform/jump_list.h"
#include "ui/app_window/app_window.h"
#include "ui/settings/settings_window.h"
#include "platform/paths.h"

namespace orchestrator {
namespace {

bool ParseIndex(std::wstring_view text, std::size_t* value) {
  if (text.empty() || value == nullptr) return false;
  std::size_t parsed = 0;
  for (const wchar_t character : text) {
    if (character < L'0' || character > L'9') return false;
    const std::size_t digit = static_cast<std::size_t>(character - L'0');
    if (parsed > (std::numeric_limits<std::size_t>::max() - digit) / 10) return false;
    parsed = parsed * 10 + digit;
  }
  *value = parsed;
  return true;
}

bool ParseReorder(const ui::application::UiValue& payload, std::size_t* from,
                  std::size_t* to) {
  const auto* text = std::get_if<std::wstring>(&payload);
  if (text == nullptr) return false;
  const std::size_t separator = text->find(L':');
  return separator != std::wstring::npos &&
         ParseIndex(std::wstring_view(*text).substr(0, separator), from) &&
         ParseIndex(std::wstring_view(*text).substr(separator + 1), to);
}

std::wstring TabsStatePath() {
  return paths::Join(paths::Join(paths::StateDir(), L"ui"), L"tabs.json");
}

}  // namespace

struct WindowOrchestrator::WindowSession {
  shell::AppWindow window;
  ui::settings::SettingsWindow settings;
  ui::containers::ParentUi parent_ui;
  ui::application::UiActionRegistry actions;
  std::unique_ptr<ModuleHostAdapter> host;
  std::vector<std::unique_ptr<modules::ModuleController>> modules;

  ~WindowSession() {
    if (host != nullptr) host->Shutdown();
    parent_ui.Detach();
  }
};

WindowOrchestrator::WindowOrchestrator(
    void* instance, const ui::config::ResolvedUiDocument* settings_document,
    const ui::config::ResolvedUiDocument* feature_document,
    const std::vector<const modules::ModuleDescriptor*>& features)
    : instance_(instance),
      settings_document_(settings_document),
      feature_document_(feature_document),
      features_(features),
      route_tabs_(std::make_unique<ui::tabs::RouteTabs>(TabsStatePath())),
      state_store_(std::make_unique<modules::ModuleStateStore>(
          paths::Join(paths::StateDir(), L"settings.json"))) {
  if (feature_document_ != nullptr) route_tabs_->Resolve(*feature_document_);
  (void)state_store_->Load();
}

WindowOrchestrator::~WindowOrchestrator() { CloseAll(); }

bool WindowOrchestrator::OpenWindow(std::wstring_view route) {
  if (instance_ == nullptr || settings_document_ == nullptr || feature_document_ == nullptr ||
      features_.empty()) {
    return false;
  }
  std::erase_if(windows_, [](const std::unique_ptr<WindowSession>& item) {
    return !item->window.alive() && (item->host == nullptr || item->host->Idle());
  });

  auto item = std::make_unique<WindowSession>();
  if (!item->window.Create(instance_, 400.0f, 360.0f)) return false;
  item->host = std::make_unique<ModuleHostAdapter>(&item->window, &item->parent_ui,
                                                   state_store_.get());
  if (!item->host->Start().ok()) return false;
  item->modules.reserve(features_.size());
  for (const modules::ModuleDescriptor* feature : features_) {
    if (feature == nullptr || feature->create == nullptr) return false;
    std::unique_ptr<modules::ModuleController> module = feature->create();
    if (module == nullptr) return false;
    item->parent_ui.ApplyPatch(module->InitialState(*item->host));
    item->modules.push_back(std::move(module));
  }
  std::wstring active_route = feature_document_->initial_route();
  if (!route.empty() && !route_tabs_->Select(route).empty()) active_route = route;
  item->parent_ui.ApplyPatch(route_tabs_->Patch(active_route));
  if (!RegisterTabActions(item.get())) return false;
  for (const auto& module : item->modules) {
    if (!module->Bind(item->host.get(), &item->actions).ok()) return false;
  }
  if (!item->parent_ui.Attach(&item->window, feature_document_, &item->actions).ok()) return false;
  WindowSession* const paired = item.get();
  item->settings.SetTabLayout(route_tabs_->multi_row(), [this](bool multi_row) {
    if (!route_tabs_->SetMultiRow(multi_row)) return;
    route_tabs_dirty_ = true;
    BroadcastTabState(nullptr);
    for (const auto& window : windows_) window->settings.ApplyTabLayout(multi_row);
    PersistTabs();
  });
  item->window.set_settings_handler([this, paired] {
    const core::Status opened =
        paired->settings.Open(instance_, paired->window.hwnd(), settings_document_);
    if (!opened.ok()) {
      MessageBoxW(static_cast<HWND>(paired->window.hwnd()), opened.Message().c_str(),
                  L"dhepz Settings", MB_OK | MB_ICONERROR);
    }
  });
  item->window.set_close_handler([this, paired] {
    if (!route_tabs_loaded_ && route_tabs_loader_ == paired) {
      route_tabs_load_started_ = false;
      route_tabs_loader_ = nullptr;
    }
    if (paired->host != nullptr) paired->host->Deactivate();
    paired->parent_ui.Detach();
    paired->settings.Close();
    paired->window.Destroy();
    if (!route_tabs_loaded_) LoadTabs();
  });

  shell::AppWindow* const window = &item->window;
  windows_.push_back(std::move(item));
  window->Show();
  LoadTabs();
  RefreshJumpList();
  return true;
}

bool WindowOrchestrator::RegisterTabActions(WindowSession* session) {
  if (session == nullptr || route_tabs_ == nullptr) return false;
  const bool selected = session->actions.Register(
      L"parent.tabs.select", [this](const ui::application::UiEvent& event,
                                    const ui::application::UiState&) {
        const auto* route = std::get_if<std::wstring>(&event.payload);
        return route != nullptr ? route_tabs_->Select(*route) : ui::application::UiPatch{};
      });
  const bool reordered = session->actions.Register(
      L"parent.tabs.reorder", [this, session](const ui::application::UiEvent& event,
                                              const ui::application::UiState& state) {
        std::size_t from = 0;
        std::size_t to = 0;
        if (!ParseReorder(event.payload, &from, &to) || !route_tabs_->Reorder(from, to)) {
          return ui::application::UiPatch{};
        }
        route_tabs_dirty_ = true;
        BroadcastTabState(session);
        PersistTabs();
        RefreshJumpList();
        return route_tabs_->Patch(state.Text(L"parent.tabs.selected"));
      });
  const bool locked = session->actions.Register(
      L"parent.tabs.lock", [this, session](const ui::application::UiEvent& event,
                                           const ui::application::UiState& state) {
        const auto* value = std::get_if<bool>(&event.payload);
        if (value == nullptr || !route_tabs_->SetLocked(*value)) {
          return ui::application::UiPatch{};
        }
        route_tabs_dirty_ = true;
        BroadcastTabState(session);
        PersistTabs();
        return route_tabs_->Patch(state.Text(L"parent.tabs.selected"));
      });
  return selected && reordered && locked;
}

void WindowOrchestrator::BroadcastTabState(WindowSession* source) {
  for (const auto& item : windows_) {
    if (item.get() == source || !item->window.alive()) continue;
    item->parent_ui.ApplyPatch(route_tabs_->Patch(item->parent_ui.active_route()));
  }
}

void WindowOrchestrator::LoadTabs() {
  if (route_tabs_loaded_ || route_tabs_load_started_ || feature_document_ == nullptr) return;
  WindowSession* loader = nullptr;
  for (const auto& item : windows_) {
    if (item->window.alive() && item->host != nullptr) {
      loader = item.get();
      break;
    }
  }
  if (loader == nullptr) return;

  route_tabs_load_started_ = true;
  route_tabs_loader_ = loader;
  auto loaded = std::make_shared<ui::tabs::RouteTabs>(TabsStatePath());
  loader->host->RunBackgroundLatest(
      L"parent-tabs-load",
      [loaded, document = feature_document_](const modules::BackgroundCapabilities&,
                                             const std::atomic<bool>& cancelled) {
        if (cancelled.load()) return core::Ok();
        const core::Status status = loaded->Load();
        if (status.ok()) loaded->Resolve(*document);
        return status;
      },
      [this, loaded, loader](const core::Status& status) {
        route_tabs_load_started_ = false;
        route_tabs_loaded_ = true;
        route_tabs_loader_ = nullptr;
        if (!status.ok()) {
          route_tabs_load_error_ = status.Message();
          if (!route_tabs_warning_shown_ && loader->window.alive()) {
            route_tabs_warning_shown_ = true;
            MessageBoxW(static_cast<HWND>(loader->window.hwnd()),
                        (L"Tab preferences could not be loaded. Defaults are active.\n\n" +
                         route_tabs_load_error_)
                            .c_str(),
                        L"dhepz", MB_OK | MB_ICONWARNING);
          }
          return;
        }
        if (route_tabs_dirty_) return;
        *route_tabs_ = *loaded;
        BroadcastTabState(nullptr);
        for (const auto& item : windows_) {
          item->settings.ApplyTabLayout(route_tabs_->multi_row());
        }
        RefreshJumpList();
      });
}

void WindowOrchestrator::PersistTabs() {
  WindowSession* writer = nullptr;
  for (const auto& item : windows_) {
    if (item->window.alive() && item->host != nullptr) {
      writer = item.get();
      break;
    }
  }
  if (writer == nullptr) return;
  const std::wstring snapshot = route_tabs_->Serialize();
  writer->host->RunBackgroundLatest(
      L"parent-tabs-persist",
      [this, snapshot](const modules::BackgroundCapabilities&,
                       const std::atomic<bool>& cancelled) {
        return cancelled.load() ? core::Ok() : route_tabs_->SaveSerialized(snapshot);
      },
      [writer](const core::Status& status) {
        if (!status.ok() && writer->window.alive()) {
          MessageBoxW(static_cast<HWND>(writer->window.hwnd()), status.Message().c_str(),
                      L"dhepz tab preferences", MB_OK | MB_ICONWARNING);
        }
      });
}

void WindowOrchestrator::RefreshJumpList() {
  WindowSession* writer = nullptr;
  for (const auto& item : windows_) {
    if (item->window.alive() && item->host != nullptr) {
      writer = item.get();
      break;
    }
  }
  if (writer == nullptr) return;
  const std::vector<std::wstring> routes = route_tabs_->order();
  const std::vector<std::wstring> labels = route_tabs_->labels();
  writer->host->RunBackgroundLatest(
      L"parent-tabs-jump-list",
      [routes, labels](const modules::BackgroundCapabilities&,
                       const std::atomic<bool>& cancelled) {
        return cancelled.load() ? core::Ok() : jump_list::Update(routes, labels);
      },
      [writer](const core::Status& status) {
        if (!status.ok() && writer->window.alive()) {
          MessageBoxW(static_cast<HWND>(writer->window.hwnd()), status.Message().c_str(),
                      L"dhepz taskbar tabs", MB_OK | MB_ICONWARNING);
        }
      });
}

void WindowOrchestrator::CloseAll() { windows_.clear(); }

}  // namespace orchestrator
