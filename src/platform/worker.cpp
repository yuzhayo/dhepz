#include "platform/worker.h"

#include <windows.h>

#include <algorithm>
#include <map>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace worker {
namespace {

// What crosses to the UI thread. The guard outlives Worker when a posted
// completion remains in the window queue during owner teardown.
struct DeliveryGuard {
  std::mutex mutex;
  std::uint64_t next_generation = 1;
  std::map<std::uint64_t, bool> generations;
};

struct CompletionMessage {
  std::shared_ptr<DeliveryGuard> guard;
  std::uint64_t generation = 0;
  std::shared_ptr<void> cargo;
  Deliver deliver;
};

struct ThreadEntry {
  std::thread thread;
  std::atomic<bool> done{false};
};

struct CoalesceSlot {
  Work work;
  Deliver deliver;
  std::uint64_t generation = 0;
  std::uint64_t sequence = 0;
  bool serving = false;
  std::shared_ptr<std::atomic<bool>> cancel;
};

bool DeliveryGenerationAlive(const std::shared_ptr<DeliveryGuard>& guard,
                             std::uint64_t generation) {
  if (generation == 0) return true;
  std::lock_guard lock(guard->mutex);
  const auto found = guard->generations.find(generation);
  return found != guard->generations.end() && found->second;
}

}  // namespace

struct Worker::Impl {
  void* ui_window = nullptr;
  unsigned int completion_message = 0;
  bool shutdown = false;

  mutable std::mutex mutex;
  std::vector<std::unique_ptr<ThreadEntry>> threads;
  std::shared_ptr<DeliveryGuard> delivery_guard = std::make_shared<DeliveryGuard>();
  std::map<std::wstring, std::shared_ptr<CoalesceSlot>> slots;

  void ReapFinishedLocked() {
    std::erase_if(threads, [](const std::unique_ptr<ThreadEntry>& entry) {
      if (!entry->done.load()) {
        return false;
      }
      entry->thread.join();
      return true;
    });
  }

  // Spawns under the caller-held lock; the entry is marked done by the
  // thread itself at the very end, so a reap never joins a live thread.
  void SpawnLocked(std::function<void()> body) {
    auto entry = std::make_unique<ThreadEntry>();
    ThreadEntry* raw = entry.get();
    entry->thread = std::thread([raw, body = std::move(body)] {
      body();
      raw->done.store(true);
    });
    threads.push_back(std::move(entry));
  }

  // Posts one completion to the UI window, or drops it when the generation
  // is already dead or the post fails (owner gone). Never touches UI state:
  // everything the UI needs travels inside the message.
  void PostCompletion(Worker* self, std::uint64_t generation, std::shared_ptr<void> cargo,
                      Deliver deliver) {
    if (generation != 0 && !self->GenerationAlive(generation)) {
      return;
    }
    auto* message = new CompletionMessage{delivery_guard, generation, std::move(cargo),
                                          std::move(deliver)};
    if (!PostMessageW(static_cast<HWND>(ui_window), completion_message, 0,
                      reinterpret_cast<LPARAM>(message))) {
      delete message;
    }
  }
};

Worker::Worker(void* ui_window, unsigned int completion_message)
    : impl_(std::make_unique<Impl>()) {
  impl_->ui_window = ui_window;
  impl_->completion_message = completion_message;
}

Worker::~Worker() { Shutdown(); }

JobHandle Worker::Submit(Work work, Deliver deliver, std::uint64_t generation) {
  JobHandle handle;
  handle.cancel = std::make_shared<std::atomic<bool>>(false);
  handle.generation = generation;

  std::lock_guard<std::mutex> guard(impl_->mutex);
  if (impl_->shutdown) {
    return handle;
  }
  impl_->ReapFinishedLocked();
  impl_->SpawnLocked([this, work = std::move(work), deliver = std::move(deliver),
                      cancel = handle.cancel, generation] {
    std::shared_ptr<void> cargo;
    if (work) {
      cargo = work(*cancel);
    }
    // Cancelled after the fact is still cancelled: the completion is
    // dropped even though the work already returned.
    if (cancel->load()) {
      return;
    }
    impl_->PostCompletion(this, generation, std::move(cargo), std::move(deliver));
  });
  return handle;
}

JobHandle Worker::SubmitCoalesced(std::wstring_view key, Work work, Deliver deliver,
                                  std::uint64_t generation, std::chrono::milliseconds quiet) {
  JobHandle handle;
  handle.generation = generation;

  std::shared_ptr<CoalesceSlot> slot;
  bool start_server = false;
  {
    std::lock_guard<std::mutex> guard(impl_->mutex);
    if (impl_->shutdown) {
      return handle;
    }
    auto& found = impl_->slots[std::wstring(key)];
    if (!found) {
      found = std::make_shared<CoalesceSlot>();
    }
    slot = found;
    // A fresh request resets any earlier cancellation of this slot.
    if (!slot->cancel || slot->cancel->load()) {
      slot->cancel = std::make_shared<std::atomic<bool>>(false);
    }
    handle.cancel = slot->cancel;
    slot->work = std::move(work);
    slot->deliver = std::move(deliver);
    slot->generation = generation;
    ++slot->sequence;
    if (!slot->serving) {
      slot->serving = true;
      start_server = true;
    }
    impl_->ReapFinishedLocked();
    if (start_server) {
      impl_->SpawnLocked([this, slot, quiet] {
        // Latest-wins settling loop. Each iteration snapshots the newest
        // request, lets it sit through the quiet period (in small slices,
        // so cancel and newer requests are seen fast), and runs it only if
        // nothing newer arrived. Five keystrokes become one run.
        for (;;) {
          Work work;
          Deliver deliver;
          std::uint64_t generation = 0;
          std::uint64_t sequence = 0;
          std::shared_ptr<std::atomic<bool>> cancel;
          {
            std::lock_guard<std::mutex> guard(impl_->mutex);
            if (!slot->serving || (slot->cancel && slot->cancel->load())) {
              break;
            }
            work = slot->work;
            deliver = slot->deliver;
            generation = slot->generation;
            sequence = slot->sequence;
            cancel = slot->cancel;
          }

          const auto deadline = std::chrono::steady_clock::now() + quiet;
          bool superseded = false;
          while (std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            std::lock_guard<std::mutex> guard(impl_->mutex);
            if ((slot->cancel && slot->cancel->load()) || !slot->serving) {
              break;
            }
            if (slot->sequence != sequence) {
              superseded = true;
              break;
            }
          }
          {
            std::lock_guard<std::mutex> guard(impl_->mutex);
            if ((slot->cancel && slot->cancel->load()) || !slot->serving) {
              break;
            }
            if (superseded || slot->sequence != sequence) {
              continue;  // a newer request won; settle it instead
            }
          }

          std::shared_ptr<void> cargo;
          if (work) {
            cargo = work(*cancel);
          }

          bool still_current = false;
          {
            std::lock_guard<std::mutex> guard(impl_->mutex);
            still_current = slot->serving && slot->sequence == sequence &&
                            !(slot->cancel && slot->cancel->load());
          }
          if (still_current) {
            impl_->PostCompletion(this, generation, std::move(cargo), std::move(deliver));
          }

          std::lock_guard<std::mutex> guard(impl_->mutex);
          if (!slot->serving || (slot->cancel && slot->cancel->load())) {
            break;
          }
          if (slot->sequence == sequence) {
            slot->serving = false;  // caught up; the slot goes quiet
            break;
          }
          // Otherwise a newer request landed while the work ran: loop and
          // settle it on this same thread.
        }
        std::lock_guard<std::mutex> guard(impl_->mutex);
        slot->serving = false;
      });
    }
  }
  return handle;
}

void Worker::Cancel(const JobHandle& handle) {
  if (handle.cancel) {
    handle.cancel->store(true);
  }
}

std::uint64_t Worker::CreateGeneration() {
  std::lock_guard<std::mutex> guard(impl_->delivery_guard->mutex);
  const std::uint64_t id = impl_->delivery_guard->next_generation++;
  impl_->delivery_guard->generations[id] = true;
  return id;
}

void Worker::InvalidateGeneration(std::uint64_t generation) {
  std::lock_guard<std::mutex> guard(impl_->delivery_guard->mutex);
  const auto found = impl_->delivery_guard->generations.find(generation);
  if (found != impl_->delivery_guard->generations.end()) {
    found->second = false;
  }
}

bool Worker::GenerationAlive(std::uint64_t generation) const {
  return DeliveryGenerationAlive(impl_->delivery_guard, generation);
}

void Worker::JoinFinished() {
  std::lock_guard<std::mutex> guard(impl_->mutex);
  impl_->ReapFinishedLocked();
}

std::size_t Worker::ThreadCount() const {
  std::lock_guard<std::mutex> guard(impl_->mutex);
  return impl_->threads.size();
}

void Worker::Shutdown() {
  std::vector<std::unique_ptr<ThreadEntry>> to_join;
  {
    std::lock_guard<std::mutex> guard(impl_->mutex);
    if (impl_->shutdown) {
      return;
    }
    impl_->shutdown = true;
    for (auto& slot : impl_->slots) {
      if (slot.second->cancel) {
        slot.second->cancel->store(true);
      }
      slot.second->serving = false;
    }
    to_join = std::move(impl_->threads);
    impl_->threads.clear();
  }
  {
    std::lock_guard<std::mutex> guard(impl_->delivery_guard->mutex);
    for (auto& generation : impl_->delivery_guard->generations) {
      generation.second = false;
    }
  }
  // Join outside the lock: a running body may still call back into the
  // worker (generation checks), and those take the same mutex.
  for (auto& entry : to_join) {
    if (entry->thread.joinable()) {
      entry->thread.join();
    }
  }
}

void Worker::Settle(long long lparam) {
  auto* message = reinterpret_cast<CompletionMessage*>(lparam);
  if (message == nullptr) {
    return;
  }
  if (DeliveryGenerationAlive(message->guard, message->generation) && message->deliver) {
    message->deliver(std::move(message->cargo));
  }
  delete message;
}

}  // namespace worker
