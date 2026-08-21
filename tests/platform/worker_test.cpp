#include "platform/worker.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "framework/test_case.h"

namespace {

// One window per test receives the completions. The window procedure routes
// the completion message to Worker::Settle; everything else is test state.
struct RigState {
  unsigned int completion_message = 0;
  unsigned int ping_message = 0;
  std::vector<std::shared_ptr<void>> delivered;
  std::atomic<int> pings{0};
  std::chrono::steady_clock::time_point ping_received;
};

RigState* g_rig = nullptr;

LRESULT CALLBACK RigProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  if (g_rig != nullptr && message == g_rig->completion_message) {
    worker::Worker::Settle(lparam);
    return 0;
  }
  if (g_rig != nullptr && message == g_rig->ping_message) {
    g_rig->ping_received = std::chrono::steady_clock::now();
    ++g_rig->pings;
    return 0;
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

struct Rig {
  HWND hwnd = nullptr;
  RigState state;

  Rig() {
    g_rig = &state;
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = RigProc;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = L"dhepz.worker.test";
    RegisterClassW(&window_class);  // already-exists is fine, one class per run
    state.completion_message = RegisterWindowMessageW(L"dhepz.worker.test.completion");
    state.ping_message = RegisterWindowMessageW(L"dhepz.worker.test.ping");
    hwnd = CreateWindowExW(0, window_class.lpszClassName, L"", WS_POPUP, 0, 0, 16, 16, nullptr,
                           nullptr, window_class.hInstance, nullptr);
    DHEPZ_CHECK(hwnd != nullptr);
  }

  ~Rig() {
    DestroyWindow(hwnd);
    g_rig = nullptr;
  }
};

// Busy-pumps until `done` or timeout. A tight loop on purpose: the tests
// measure how fast a message gets through, and sleeping would smear it.
void PumpUntil(const std::function<bool()>& done, int timeout_ms) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (!done() && std::chrono::steady_clock::now() < deadline) {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    std::this_thread::yield();
  }
}

std::shared_ptr<void> MakeInt(int value) { return std::make_shared<int>(value); }

int DeliveredInt(const RigState& state, std::size_t index) {
  return *std::static_pointer_cast<int>(state.delivered[index]);
}

}  // namespace

DHEPZ_TEST(Worker, DeliversCompletionOnTheUiThread) {
  Rig rig;
  worker::Worker worker(rig.hwnd, rig.state.completion_message);
  const std::thread::id ui_thread = std::this_thread::get_id();
  std::thread::id delivery_thread;

  worker.Submit(
      [](const std::atomic<bool>&) { return MakeInt(42); },
      [&](std::shared_ptr<void> cargo) {
        delivery_thread = std::this_thread::get_id();
        rig.state.delivered.push_back(std::move(cargo));
      });

  PumpUntil([&] { return rig.state.delivered.size() == 1; }, 5000);
  DHEPZ_CHECK_EQ(rig.state.delivered.size(), static_cast<std::size_t>(1));
  DHEPZ_CHECK_EQ(DeliveredInt(rig.state, 0), 42);
  DHEPZ_CHECK(delivery_thread == ui_thread);
  worker.Shutdown();
}

DHEPZ_TEST(Worker, ThreadCountReturnsToZeroAtIdle) {
  Rig rig;
  worker::Worker worker(rig.hwnd, rig.state.completion_message);

  worker.Submit(
      [](const std::atomic<bool>&) {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        return MakeInt(1);
      },
      [&](std::shared_ptr<void> cargo) { rig.state.delivered.push_back(std::move(cargo)); });

  DHEPZ_CHECK(worker.ThreadCount() >= 1);
  PumpUntil([&] { return rig.state.delivered.size() == 1; }, 5000);
  // The worker marks its thread done just after posting the completion, so
  // the registry catches up a hair after delivery; join in a short retry
  // loop instead of racing it.
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);
  while (worker.ThreadCount() > 0 && std::chrono::steady_clock::now() < deadline) {
    worker.JoinFinished();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  DHEPZ_CHECK_EQ(worker.ThreadCount(), static_cast<std::size_t>(0));
  worker.Shutdown();
}

DHEPZ_TEST(Worker, StaleGenerationIsDropped) {
  Rig rig;
  worker::Worker worker(rig.hwnd, rig.state.completion_message);
  const std::uint64_t generation = worker.CreateGeneration();

  worker.Submit(
      [](const std::atomic<bool>&) {
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        return MakeInt(7);
      },
      [&](std::shared_ptr<void> cargo) { rig.state.delivered.push_back(std::move(cargo)); },
      generation);

  // The "window" closes before the result lands.
  worker.InvalidateGeneration(generation);

  PumpUntil([&] { return false; }, 400);  // give the post every chance to arrive
  DHEPZ_CHECK(rig.state.delivered.empty());
  DHEPZ_CHECK_FALSE(worker.GenerationAlive(generation));
  worker.JoinFinished();
  DHEPZ_CHECK_EQ(worker.ThreadCount(), static_cast<std::size_t>(0));
  worker.Shutdown();
}

DHEPZ_TEST(Worker, CancelStopsTheJobAndDiscardsItsCompletion) {
  Rig rig;
  worker::Worker worker(rig.hwnd, rig.state.completion_message);
  std::atomic<bool> observed_cancel{false};

  const worker::JobHandle handle = worker.Submit(
      [&](const std::atomic<bool>& cancelled) -> std::shared_ptr<void> {
        for (int i = 0; i < 2000; ++i) {
          if (cancelled.load()) {
            observed_cancel.store(true);
            return nullptr;
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return MakeInt(9);
      },
      [&](std::shared_ptr<void> cargo) { rig.state.delivered.push_back(std::move(cargo)); });

  std::this_thread::sleep_for(std::chrono::milliseconds(40));
  worker.Cancel(handle);

  PumpUntil([&] { return observed_cancel.load(); }, 3000);
  DHEPZ_CHECK(observed_cancel.load());
  PumpUntil([&] { return false; }, 200);  // nothing may still be delivered
  DHEPZ_CHECK(rig.state.delivered.empty());
  worker.JoinFinished();
  DHEPZ_CHECK_EQ(worker.ThreadCount(), static_cast<std::size_t>(0));
  worker.Shutdown();
}

DHEPZ_TEST(Worker, CoalescesRapidRequestsToTheLatest) {
  Rig rig;
  worker::Worker worker(rig.hwnd, rig.state.completion_message);
  std::atomic<int> run_count{0};

  for (int i = 0; i < 5; ++i) {
    worker.SubmitCoalesced(
        L"folder-box",
        [&, i](const std::atomic<bool>&) {
          ++run_count;
          return MakeInt(i);
        },
        [&](std::shared_ptr<void> cargo) { rig.state.delivered.push_back(std::move(cargo)); },
        0, std::chrono::milliseconds(80));
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
  }

  PumpUntil([&] { return !rig.state.delivered.empty(); }, 5000);
  PumpUntil([&] { return false; }, 250);  // settle window for any stragglers
  // Five keystrokes become at most two runs, and the value that lands is
  // the latest request.
  DHEPZ_CHECK(run_count.load() <= 2);
  DHEPZ_CHECK(!rig.state.delivered.empty());
  DHEPZ_CHECK_EQ(DeliveredInt(rig.state, rig.state.delivered.size() - 1), 4);
  worker.JoinFinished();
  worker.Shutdown();
}

DHEPZ_TEST(Worker, BlockingJobNeverStallsTheMessagePump) {
  Rig rig;
  worker::Worker worker(rig.hwnd, rig.state.completion_message);

  worker.Submit(
      [](const std::atomic<bool>&) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        return MakeInt(0);
      },
      [&](std::shared_ptr<void> cargo) { rig.state.delivered.push_back(std::move(cargo)); });

  // While the 2 s job is in flight, an unrelated message must still get
  // through fast — the stand-in for "a paint is never delayed".
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  const auto posted = std::chrono::steady_clock::now();
  PostMessageW(rig.hwnd, rig.state.ping_message, 0, 0);
  PumpUntil([&] { return rig.state.pings.load() > 0; }, 2000);
  const auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(
                           rig.state.ping_received - posted)
                           .count();
  DHEPZ_CHECK(rig.state.pings.load() > 0);
  DHEPZ_CHECK(latency < 100);
  DHEPZ_CHECK(rig.state.delivered.empty());  // the 2 s job is still running
  worker.Shutdown();  // joins the long job; ~2 s by design
}

DHEPZ_TEST(Worker, SequentialJobsLeaveTheHandleCountFlat) {
  Rig rig;
  worker::Worker worker(rig.hwnd, rig.state.completion_message);
  const HANDLE process = GetCurrentProcess();

  // Warm-up first: thread creation at the start of the test is stabilisation,
  // not leakage, and machine noise there flaked the measurement.
  for (int i = 0; i < 100; ++i) {
    worker.Submit(
        [](const std::atomic<bool>&) { return MakeInt(1); },
        [&](std::shared_ptr<void> cargo) { rig.state.delivered.push_back(std::move(cargo)); });
    PumpUntil([&] { return rig.state.delivered.size() == static_cast<std::size_t>(i + 1); }, 5000);
  }
  worker.JoinFinished();

  DWORD handles_before = 0;
  GetProcessHandleCount(process, &handles_before);

  for (int i = 100; i < 1000; ++i) {
    worker.Submit(
        [](const std::atomic<bool>&) { return MakeInt(1); },
        [&](std::shared_ptr<void> cargo) { rig.state.delivered.push_back(std::move(cargo)); });
    PumpUntil([&] { return rig.state.delivered.size() == static_cast<std::size_t>(i + 1); }, 5000);
    if (i % 100 == 99) {
      worker.JoinFinished();
    }
  }
  worker.JoinFinished();

  DWORD handles_after = 0;
  GetProcessHandleCount(process, &handles_after);
  DHEPZ_CHECK_EQ(rig.state.delivered.size(), static_cast<std::size_t>(1000));
  DHEPZ_CHECK_EQ(worker.ThreadCount(), static_cast<std::size_t>(0));
  // Flat means "no accumulated thread handles"; allow the OS a few of its
  // own movements but nothing proportional to the job count.
  const long long drift = static_cast<long long>(handles_after) - handles_before;
  DHEPZ_CHECK(drift < 10);
  worker.Shutdown();
}

DHEPZ_TEST(Worker, ShutdownJoinsCleanlyAndDiscardsInFlightCompletions) {
  Rig rig;
  worker::Worker worker(rig.hwnd, rig.state.completion_message);
  const std::uint64_t generation = worker.CreateGeneration();

  worker.Submit(
      [](const std::atomic<bool>&) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        return MakeInt(3);
      },
      [&](std::shared_ptr<void> cargo) { rig.state.delivered.push_back(std::move(cargo)); },
      generation);

  // Shutdown invalidates every generation, so the in-flight job finishes
  // (its thread is joined — no terminate, no leak) but its result can never
  // land on a UI that is being torn down.
  worker.Shutdown();
  DHEPZ_CHECK_EQ(worker.ThreadCount(), static_cast<std::size_t>(0));
  PumpUntil([&] { return false; }, 150);
  DHEPZ_CHECK(rig.state.delivered.empty());

  // After shutdown a submit is a no-op: no thread, no delivery.
  worker.Submit([](const std::atomic<bool>&) { return MakeInt(4); },
                [&](std::shared_ptr<void> cargo) { rig.state.delivered.push_back(std::move(cargo)); });
  DHEPZ_CHECK_EQ(worker.ThreadCount(), static_cast<std::size_t>(0));
  PumpUntil([&] { return false; }, 150);
  DHEPZ_CHECK(rig.state.delivered.empty());
}

DHEPZ_TEST(Worker, ADeadWindowDiscardsTheCompletionWithoutCrashing) {
  HWND window = nullptr;
  {
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = DefWindowProcW;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = L"dhepz.worker.test.dead";
    RegisterClassW(&window_class);
    window = CreateWindowExW(0, window_class.lpszClassName, L"", WS_POPUP, 0, 0, 16, 16, nullptr,
                             nullptr, window_class.hInstance, nullptr);
  }
  DHEPZ_CHECK(window != nullptr);
  const unsigned int message = RegisterWindowMessageW(L"dhepz.worker.test.dead.completion");
  DestroyWindow(window);  // the owner is gone before the job finishes

  worker::Worker worker(window, message);
  worker.Submit(
      [](const std::atomic<bool>&) { return MakeInt(5); },
      [](std::shared_ptr<void>) { DHEPZ_FAIL("delivery to a dead window must not happen"); });

  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  worker.JoinFinished();
  DHEPZ_CHECK_EQ(worker.ThreadCount(), static_cast<std::size_t>(0));
  worker.Shutdown();
}
