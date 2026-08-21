#include "modules/gate/settings_store.h"

#include <algorithm>
#include <mutex>
#include <utility>

#include "core/json.h"
#include "platform/files.h"
#include "platform/paths.h"
#include "platform/worker.h"

namespace modules {
namespace {

json::Value DefaultSnapshot() {
  json::Value root = json::Value::Object();
  root.Set(L"global", json::Value::Object());
  root.Set(L"modules", json::Value::Object());
  return root;
}

core::Status ValidateSnapshot(const json::Value& root) {
  if (!root.is_object()) {
    return DHEPZ_ERR(core::ErrorCode::ParseError,
                     L"settings root must be an object");
  }
  const json::Value* global = root.Find(L"global");
  if (global != nullptr && !global->is_object()) {
    return DHEPZ_ERR(core::ErrorCode::ParseError,
                     L"settings global section must be an object");
  }
  const json::Value* modules_value = root.Find(L"modules");
  if (modules_value != nullptr && !modules_value->is_object()) {
    return DHEPZ_ERR(core::ErrorCode::ParseError,
                     L"settings modules section must be an object");
  }
  if (modules_value != nullptr) {
    for (const auto& [module_id, section] : modules_value->members()) {
      (void)module_id;
      if (!section.is_object()) {
        return DHEPZ_ERR(core::ErrorCode::ParseError,
                         L"every module settings section must be an object");
      }
    }
  }
  return core::Ok();
}

void EnsureShape(json::Value* root) {
  if (root->Find(L"global") == nullptr) root->Set(L"global", json::Value::Object());
  if (root->Find(L"modules") == nullptr) root->Set(L"modules", json::Value::Object());
}

json::Value SettingValue(std::wstring_view text) {
  json::Value parsed;
  if (json::Parse(text, &parsed).ok()) return parsed;
  return json::Value::String(std::wstring(text));
}

core::Status ReadSetting(const json::Value* section, std::wstring_view key,
                         std::wstring* out) {
  if (out == nullptr) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                     L"A settings output is required");
  }
  out->clear();
  if (section == nullptr || !section->is_object()) {
    return DHEPZ_ERR(core::ErrorCode::NotFound, L"Settings section is unavailable");
  }
  const json::Value* value = section->Find(key);
  if (value == nullptr) {
    return DHEPZ_ERR(core::ErrorCode::NotFound, L"Setting is absent");
  }
  *out = value->is_string() ? value->AsString() : json::Serialize(*value, false);
  return core::Ok();
}

core::Status ValidateAsync(const SettingsStoreCallback& callback,
                           AsyncRequestToken* token) {
  if (token == nullptr) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                     L"A request-token output is required");
  }
  *token = {};
  if (!callback) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                     L"A settings completion callback is required");
  }
  return core::Ok();
}

struct LoadCargo {
  json::Value snapshot = DefaultSnapshot();
  core::Status status;
  bool used_defaults = false;
};

struct SaveCargo {
  std::uint64_t revision = 0;
  core::Status status;
};

struct PendingSave {
  AsyncRequestToken token;
  std::uint64_t revision = 0;
  SettingsStoreCallback callback;
};

struct PendingLoad {
  AsyncRequestToken token;
  SettingsStoreCallback callback;
};

struct LatestWrite {
  std::mutex mutex;
  std::wstring text;
  std::uint64_t revision = 0;
};

}  // namespace

struct SettingsStore::Impl {
  Impl(void* window, unsigned int message, std::wstring configured_path)
      : ui_window(window), completion_message(message) {
    default_path = configured_path.empty();
    path = default_path ? paths::Join(paths::StateDir(), L"settings.json")
                        : std::move(configured_path);
  }

  void EnsureWorker() {
    if (worker) return;
    worker = std::make_unique<worker::Worker>(ui_window, completion_message);
    generation = worker->CreateGeneration();
  }

  void StartLatestWrite() {
    if (write_active || !ready || completed_revision >= revision) return;
    EnsureWorker();
    write_active = true;
    const std::wstring target = path;
    const bool ensure_default = default_path;
    const std::shared_ptr<LatestWrite> latest = latest_write;
    worker->Submit(
        [latest, target, ensure_default](const std::atomic<bool>& cancelled) {
          auto result = std::make_shared<SaveCargo>();
          // Drain to the newest snapshot on this one run-once thread. Shutdown
          // may suppress delivery, but it does not abandon the newest complete
          // snapshot already accepted on the UI thread.
          for (;;) {
            std::wstring text;
            std::uint64_t revision = 0;
            {
              std::lock_guard lock(latest->mutex);
              text = latest->text;
              revision = latest->revision;
            }
            (void)cancelled;
            result->revision = revision;
            result->status = core::Ok();
            if (target.empty()) {
              result->status = core::Err(core::ErrorCode::IoError,
                                         L"Settings path is unavailable");
            } else {
              if (ensure_default) result->status = paths::EnsureStateDir();
              if (result->status.ok()) {
                result->status = files::WriteTextAtomic(target, text);
              }
            }
            std::lock_guard lock(latest->mutex);
            if (latest->revision == revision) break;
          }
          return std::static_pointer_cast<void>(result);
        },
        [this](std::shared_ptr<void> cargo) {
          const auto result = std::static_pointer_cast<SaveCargo>(std::move(cargo));
          write_active = false;
          completed_revision = result->revision;
          if (!result->status.ok()) {
            diagnostics.push_back(
                {SettingsStoreOperation::Save, result->revision, result->status});
          }

          std::vector<PendingSave> remaining;
          for (PendingSave& pending : pending_saves) {
            if (pending.revision <= result->revision) {
              pending.callback({pending.token, SettingsStoreOperation::Save,
                                result->revision, false, result->status});
            } else {
              remaining.push_back(std::move(pending));
            }
          }
          pending_saves = std::move(remaining);
          // A newer in-memory snapshot may have arrived while this write ran.
          // It starts only after the older write completed, so completion
          // order can never roll the file backward.
          StartLatestWrite();
        },
        generation);
  }

  core::Status StartWrite(std::wstring_view module_id, std::wstring_view key,
                          std::wstring_view value, SettingsStoreCallback callback,
                          AsyncRequestToken* token) {
    DHEPZ_RETURN_IF_ERROR(ValidateAsync(callback, token));
    if (!ready) {
      return DHEPZ_ERR(core::ErrorCode::NotFound,
                       L"Settings are not ready; call StartSettingsLoad first");
    }
    if (key.empty()) {
      return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                       L"A setting key is required");
    }
    EnsureShape(&snapshot);
    if (module_id.empty()) {
      snapshot.Find(L"global")->Set(key, SettingValue(value));
    } else {
      json::Value* modules_value = snapshot.Find(L"modules");
      json::Value* section = modules_value->Find(module_id);
      if (section == nullptr) {
        modules_value->Set(module_id, json::Value::Object());
        section = modules_value->Find(module_id);
      }
      section->Set(key, SettingValue(value));
    }
    ++revision;
    {
      std::lock_guard lock(latest_write->mutex);
      latest_write->text = json::Serialize(snapshot, true);
      latest_write->revision = revision;
    }
    AsyncRequestToken issued{next_token++};
    pending_saves.push_back({issued, revision, std::move(callback)});
    *token = issued;
    StartLatestWrite();
    return core::Ok();
  }

  void* ui_window = nullptr;
  unsigned int completion_message = 0;
  std::wstring path;
  bool default_path = false;
  std::unique_ptr<worker::Worker> worker;
  std::uint64_t generation = 0;
  // Keep store tokens disjoint from per-module host-operation tokens so a
  // single CancelRequest call can safely be routed to both owners.
  std::uint64_t next_token = std::uint64_t{1} << 63;
  worker::JobHandle load_handle;
  std::vector<PendingLoad> load_waiters;
  core::Status load_status;
  bool load_used_defaults = false;
  json::Value snapshot = DefaultSnapshot();
  bool loading = false;
  bool ready = false;
  bool write_active = false;
  std::uint64_t revision = 0;
  std::uint64_t completed_revision = 0;
  std::shared_ptr<LatestWrite> latest_write = std::make_shared<LatestWrite>();
  std::vector<PendingSave> pending_saves;
  std::vector<SettingsStoreDiagnostic> diagnostics;
};

SettingsStore::SettingsStore(void* ui_window, unsigned int completion_message,
                             std::wstring path)
    : impl_(std::make_unique<Impl>(ui_window, completion_message, std::move(path))) {}

SettingsStore::~SettingsStore() = default;

core::Status SettingsStore::StartLoad(SettingsStoreCallback callback,
                                      AsyncRequestToken* token) {
  DHEPZ_RETURN_IF_ERROR(ValidateAsync(callback, token));
  const AsyncRequestToken issued{impl_->next_token++};
  *token = issued;
  if (impl_->ready) {
    callback({issued, SettingsStoreOperation::Load, 0,
              impl_->load_used_defaults, impl_->load_status});
    return core::Ok();
  }
  impl_->load_waiters.push_back({issued, std::move(callback)});
  if (impl_->loading) return core::Ok();
  impl_->EnsureWorker();
  impl_->loading = true;
  const std::wstring path = impl_->path;
  impl_->load_handle = impl_->worker->Submit(
      [path](const std::atomic<bool>& cancelled) {
        auto result = std::make_shared<LoadCargo>();
        if (cancelled.load()) {
          result->status =
              core::Err(core::ErrorCode::Cancelled, L"Settings load was cancelled");
          result->used_defaults = true;
          return std::static_pointer_cast<void>(result);
        }
        std::wstring text;
        result->status = path.empty()
                             ? core::Err(core::ErrorCode::IoError,
                                         L"Settings path is unavailable")
                             : files::ReadText(path, &text);
        if (result->status.ok()) {
          result->status = json::Parse(text, &result->snapshot);
        }
        if (result->status.ok()) result->status = ValidateSnapshot(result->snapshot);
        if (!result->status.ok()) {
          result->snapshot = DefaultSnapshot();
          result->used_defaults = true;
        } else {
          EnsureShape(&result->snapshot);
        }
        return std::static_pointer_cast<void>(result);
      },
      [this](std::shared_ptr<void> cargo) {
        const auto result = std::static_pointer_cast<LoadCargo>(std::move(cargo));
        impl_->loading = false;
        impl_->ready = true;
        impl_->snapshot = std::move(result->snapshot);
        impl_->load_status = result->status;
        impl_->load_used_defaults = result->used_defaults;
        if (!result->status.ok()) {
          impl_->diagnostics.push_back(
              {SettingsStoreOperation::Load, 0, result->status});
        }
        impl_->load_handle = {};
        std::vector<PendingLoad> waiters = std::move(impl_->load_waiters);
        impl_->load_waiters.clear();
        for (PendingLoad& waiter : waiters) {
          waiter.callback({waiter.token, SettingsStoreOperation::Load, 0,
                           result->used_defaults, result->status});
        }
      },
      impl_->generation);
  return core::Ok();
}

core::Status SettingsStore::ReadGlobal(std::wstring_view key, std::wstring* out) const {
  if (out != nullptr) out->clear();
  if (!impl_->ready) {
    return DHEPZ_ERR(core::ErrorCode::NotFound, L"Settings are not ready");
  }
  return ReadSetting(impl_->snapshot.Find(L"global"), key, out);
}

core::Status SettingsStore::ReadModule(std::wstring_view module_id,
                                       std::wstring_view key,
                                       std::wstring* out) const {
  if (out != nullptr) out->clear();
  if (!impl_->ready) {
    return DHEPZ_ERR(core::ErrorCode::NotFound, L"Settings are not ready");
  }
  const json::Value* modules_value = impl_->snapshot.Find(L"modules");
  const json::Value* section =
      modules_value != nullptr ? modules_value->Find(module_id) : nullptr;
  return ReadSetting(section, key, out);
}

core::Status SettingsStore::StartWriteGlobal(std::wstring_view key,
                                             std::wstring_view value,
                                             SettingsStoreCallback callback,
                                             AsyncRequestToken* token) {
  return impl_->StartWrite({}, key, value, std::move(callback), token);
}

core::Status SettingsStore::StartWriteModule(std::wstring_view module_id,
                                             std::wstring_view key,
                                             std::wstring_view value,
                                             SettingsStoreCallback callback,
                                             AsyncRequestToken* token) {
  if (module_id.empty()) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                     L"A module id is required for module settings");
  }
  return impl_->StartWrite(module_id, key, value, std::move(callback), token);
}

void SettingsStore::CancelRequest(AsyncRequestToken token) {
  if (!token) return;
  const std::size_t before = impl_->load_waiters.size();
  std::erase_if(impl_->load_waiters,
                [token](const PendingLoad& pending) { return pending.token == token; });
  if (impl_->load_waiters.size() != before) {
    if (impl_->loading && impl_->load_waiters.empty()) {
      impl_->worker->Cancel(impl_->load_handle);
      impl_->loading = false;
      impl_->load_handle = {};
    }
    return;
  }
  std::erase_if(impl_->pending_saves,
                [token](const PendingSave& pending) { return pending.token == token; });
}

bool SettingsStore::ready() const { return impl_->ready; }

const std::vector<SettingsStoreDiagnostic>& SettingsStore::diagnostics() const {
  return impl_->diagnostics;
}

void SettingsStore::ReapFinished() {
  if (impl_->worker) impl_->worker->JoinFinished();
}

std::size_t SettingsStore::ThreadCount() const {
  return impl_->worker ? impl_->worker->ThreadCount() : 0;
}

}  // namespace modules
