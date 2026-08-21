#include "modules/gate/settings_store.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include "core/json.h"
#include "framework/test_case.h"
#include "platform/files.h"
#include "platform/paths.h"
#include "platform/worker.h"

namespace {

struct RigState {
  unsigned int completion_message = 0;
};
RigState* g_settings_rig = nullptr;

LRESULT CALLBACK SettingsRigProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  if (g_settings_rig != nullptr && message == g_settings_rig->completion_message) {
    worker::Worker::Settle(lparam);
    return 0;
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

struct Rig {
  HWND window = nullptr;
  RigState state;
  Rig() {
    g_settings_rig = &state;
    WNDCLASSW cls{};
    cls.lpfnWndProc = SettingsRigProc;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = L"dhepz.settings-store.test";
    RegisterClassW(&cls);
    state.completion_message = RegisterWindowMessageW(L"dhepz.settings-store.completion");
    window = CreateWindowExW(0, cls.lpszClassName, L"", WS_POPUP, 0, 0, 16, 16,
                             nullptr, nullptr, cls.hInstance, nullptr);
    DHEPZ_CHECK(window != nullptr);
  }
  ~Rig() {
    if (window != nullptr) DestroyWindow(window);
    g_settings_rig = nullptr;
  }
};

template <typename Predicate>
void PumpUntil(Predicate predicate, int timeout_ms = 3000) {
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    if (predicate()) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

std::wstring UniquePath(std::wstring_view leaf) {
  wchar_t base[MAX_PATH]{};
  GetTempPathW(MAX_PATH, base);
  const std::wstring dir = std::wstring(base) + L"dhepz-settings-store-" +
                           std::to_wstring(GetTickCount64()) + L"-" +
                           std::to_wstring(reinterpret_cast<std::uintptr_t>(&base));
  return paths::Join(dir, leaf);
}

void Load(modules::SettingsStore& store, modules::SettingsStoreCompletion* completion) {
  std::atomic<bool> done{false};
  modules::AsyncRequestToken token;
  DHEPZ_CHECK(store.StartLoad(
                        [&](const modules::SettingsStoreCompletion& value) {
                          *completion = value;
                          done = true;
                        },
                        &token)
                  .ok());
  DHEPZ_CHECK(token);
  PumpUntil([&] { return done.load(); });
  DHEPZ_CHECK(done.load());
}

}  // namespace

DHEPZ_TEST(SettingsStore, OnePhysicalLoadCompletesEveryModuleWaiter) {
  Rig rig;
  const std::wstring path = UniquePath(L"waiters\\settings.json");
  modules::SettingsStore store(rig.window, rig.state.completion_message, path);
  std::atomic<int> ready_count{0};
  modules::AsyncRequestToken first;
  modules::AsyncRequestToken second;
  DHEPZ_CHECK(store.StartLoad(
                        [&](const modules::SettingsStoreCompletion&) { ++ready_count; },
                        &first)
                  .ok());
  DHEPZ_CHECK(store.StartLoad(
                        [&](const modules::SettingsStoreCompletion&) { ++ready_count; },
                        &second)
                  .ok());
  DHEPZ_CHECK(first != second);
  PumpUntil([&] { return ready_count.load() == 2; });
  DHEPZ_CHECK_EQ(ready_count.load(), 2);

  modules::AsyncRequestToken late;
  DHEPZ_CHECK(store.StartLoad(
                        [&](const modules::SettingsStoreCompletion&) { ++ready_count; },
                        &late)
                  .ok());
  DHEPZ_CHECK(late != first && late != second);
  DHEPZ_CHECK_EQ(ready_count.load(), 3);
}

DHEPZ_TEST(SettingsStore, PersistenceFailureCompletesWithStatusAndDiagnostic) {
  Rig rig;
  const std::wstring blocker = UniquePath(L"blocker");
  DHEPZ_CHECK(files::WriteTextAtomic(blocker, L"not a directory").ok());
  modules::SettingsStore store(rig.window, rig.state.completion_message,
                               paths::Join(blocker, L"settings.json"));
  modules::SettingsStoreCompletion loaded;
  Load(store, &loaded);
  std::atomic<bool> done{false};
  modules::SettingsStoreCompletion saved;
  modules::AsyncRequestToken token;
  DHEPZ_CHECK(store.StartWriteGlobal(
                        L"theme", L"dark",
                        [&](const modules::SettingsStoreCompletion& completion) {
                          saved = completion;
                          done = true;
                        },
                        &token)
                  .ok());
  PumpUntil([&] { return done.load(); });
  DHEPZ_CHECK(done.load());
  DHEPZ_CHECK(!saved.status.ok());
  DHEPZ_CHECK(saved.operation == modules::SettingsStoreOperation::Save);
  DHEPZ_CHECK(!store.diagnostics().empty());
  DHEPZ_CHECK(store.diagnostics().back().status.Code() == saved.status.Code());
  PumpUntil([] { return false; }, 100);
  store.ReapFinished();
  DHEPZ_CHECK_EQ(store.diagnostics().size(), static_cast<std::size_t>(2));
  DHEPZ_CHECK_EQ(store.ThreadCount(), static_cast<std::size_t>(0));
}

DHEPZ_TEST(SettingsStore, ShutdownFlushesOnlyTheNewestAcceptedSnapshot) {
  Rig rig;
  const std::wstring path = UniquePath(L"shutdown\\settings.json");
  {
    modules::SettingsStore store(rig.window, rig.state.completion_message, path);
    modules::SettingsStoreCompletion loaded;
    Load(store, &loaded);
    modules::AsyncRequestToken first;
    modules::AsyncRequestToken second;
    DHEPZ_CHECK(store.StartWriteModule(L"terminal", L"value", L"old",
                                      [](const auto&) {}, &first)
                    .ok());
    DHEPZ_CHECK(store.StartWriteModule(L"terminal", L"value", L"new",
                                      [](const auto&) {}, &second)
                    .ok());
    // No message pumping: destruction is the graceful-shutdown path.
  }
  modules::SettingsStore reloaded(rig.window, rig.state.completion_message, path);
  modules::SettingsStoreCompletion loaded;
  Load(reloaded, &loaded);
  std::wstring value;
  DHEPZ_CHECK(reloaded.ReadModule(L"terminal", L"value", &value).ok());
  DHEPZ_CHECK_EQ(value, std::wstring(L"new"));
}

DHEPZ_TEST(SettingsStore, ConstructionIsInertAndMissingLoadUsesDefaults) {
  Rig rig;
  const std::wstring path = UniquePath(L"missing\\settings.json");
  const std::wstring parent = paths::Parent(path);
  modules::SettingsStore store(rig.window, rig.state.completion_message, path);
  DHEPZ_CHECK(!paths::DirectoryExists(parent));
  DHEPZ_CHECK_EQ(store.ThreadCount(), static_cast<std::size_t>(0));

  std::wstring value = L"not-cleared";
  DHEPZ_CHECK(!store.ReadGlobal(L"theme", &value).ok());
  DHEPZ_CHECK(value.empty());

  modules::SettingsStoreCompletion completion;
  Load(store, &completion);
  DHEPZ_CHECK(completion.used_defaults);
  DHEPZ_CHECK(!completion.status.ok());
  DHEPZ_CHECK(store.ready());
  DHEPZ_CHECK(!store.diagnostics().empty());
  DHEPZ_CHECK(!paths::DirectoryExists(parent));
}

DHEPZ_TEST(SettingsStore, PersistsAcrossOwnersAndPreservesUnknownSections) {
  Rig rig;
  const std::wstring path = UniquePath(L"persist\\settings.json");
  DHEPZ_CHECK(files::WriteTextAtomic(
                  path,
                  LR"({"global":{"theme":"light"},"modules":{"future":{"opaque":{"x":1}}}})")
                  .ok());
  {
    modules::SettingsStore first(rig.window, rig.state.completion_message, path);
    modules::SettingsStoreCompletion loaded;
    Load(first, &loaded);
    DHEPZ_CHECK(loaded.status.ok());
    std::atomic<bool> saved{false};
    modules::AsyncRequestToken token;
    DHEPZ_CHECK(first.StartWriteModule(
                          L"terminal", L"recent_folders", LR"(["C:\\work"])",
                          [&](const modules::SettingsStoreCompletion& completion) {
                            DHEPZ_CHECK(completion.status.ok());
                            saved = true;
                          },
                          &token)
                    .ok());
    PumpUntil([&] { return saved.load(); });
    DHEPZ_CHECK(saved.load());
  }
  {
    modules::SettingsStore second(rig.window, rig.state.completion_message, path);
    modules::SettingsStoreCompletion loaded;
    Load(second, &loaded);
    std::wstring recent;
    DHEPZ_CHECK(second.ReadModule(L"terminal", L"recent_folders", &recent).ok());
    DHEPZ_CHECK_EQ(recent, std::wstring(LR"(["C:\\work"])") );
    std::wstring text;
    DHEPZ_CHECK(files::ReadText(path, &text).ok());
    json::Value root;
    DHEPZ_CHECK(json::Parse(text, &root).ok());
    const json::Value* modules_value = root.Find(L"modules");
    DHEPZ_CHECK(modules_value != nullptr);
    const json::Value* future = modules_value->Find(L"future");
    DHEPZ_CHECK(future != nullptr && future->Find(L"opaque") != nullptr);
  }
}

DHEPZ_TEST(SettingsStore, RapidWritesPersistNewestCompleteSnapshot) {
  Rig rig;
  const std::wstring path = UniquePath(L"rapid\\settings.json");
  modules::SettingsStore store(rig.window, rig.state.completion_message, path);
  modules::SettingsStoreCompletion loaded;
  Load(store, &loaded);

  std::atomic<int> completions{0};
  modules::AsyncRequestToken first_token;
  modules::AsyncRequestToken second_token;
  const std::wstring large(1024 * 1024, L'a');
  DHEPZ_CHECK(store.StartWriteModule(
                        L"terminal", L"value", large,
                        [&](const modules::SettingsStoreCompletion&) { ++completions; },
                        &first_token)
                  .ok());
  DHEPZ_CHECK(store.StartWriteModule(
                        L"terminal", L"value", L"newest",
                        [&](const modules::SettingsStoreCompletion&) { ++completions; },
                        &second_token)
                  .ok());
  DHEPZ_CHECK(first_token != second_token);
  PumpUntil([&] { return completions.load() == 2; }, 6000);
  DHEPZ_CHECK_EQ(completions.load(), 2);

  modules::SettingsStore reloaded(rig.window, rig.state.completion_message, path);
  Load(reloaded, &loaded);
  std::wstring value;
  DHEPZ_CHECK(reloaded.ReadModule(L"terminal", L"value", &value).ok());
  DHEPZ_CHECK_EQ(value, std::wstring(L"newest"));
}

DHEPZ_TEST(SettingsStore, CorruptFileReportsDiagnosticAndKeepsDefaults) {
  Rig rig;
  const std::wstring path = UniquePath(L"corrupt\\settings.json");
  DHEPZ_CHECK(files::WriteTextAtomic(path, L"{ broken").ok());
  modules::SettingsStore store(rig.window, rig.state.completion_message, path);
  modules::SettingsStoreCompletion loaded;
  Load(store, &loaded);
  DHEPZ_CHECK(store.ready());
  DHEPZ_CHECK(loaded.used_defaults);
  DHEPZ_CHECK(!loaded.status.ok());
  DHEPZ_CHECK(loaded.status.Code() == core::ErrorCode::ParseError);
  DHEPZ_CHECK_EQ(store.diagnostics().size(), static_cast<std::size_t>(1));
}
