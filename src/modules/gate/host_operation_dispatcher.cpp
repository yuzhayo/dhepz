#include "modules/gate/host_operation_dispatcher.h"

#include <windows.h>

#include <map>
#include <mutex>
#include <utility>
#include <vector>

#include "platform/files.h"
#include "platform/paths.h"
#include "platform/process.h"
#include "platform/strings.h"
#include "platform/worker.h"

namespace modules {
namespace {

HostOperationKind OperationKind(ProcessOperation operation) {
  switch (operation) {
    case ProcessOperation::Launch:
      return HostOperationKind::Launch;
    case ProcessOperation::ElevatedLaunch:
      return HostOperationKind::ElevatedLaunch;
    case ProcessOperation::Capture:
      return HostOperationKind::Capture;
  }
  return HostOperationKind::Launch;
}

bool ContainsInvalidExecutableCharacter(std::wstring_view executable) {
  return executable.find_first_of(std::wstring_view(L"\0\"\r\n", 4)) !=
         std::wstring_view::npos;
}

std::wstring BuildArgumentList(const std::vector<std::wstring>& arguments) {
  std::wstring command_line;
  for (const std::wstring& argument : arguments) {
    if (!command_line.empty()) command_line.push_back(L' ');
    command_line.append(str::QuoteArg(argument));
  }
  return command_line;
}

bool SafeRelativeFile(std::wstring_view path) {
  if (path.empty() || path.front() == L'\\' || path.front() == L'/' ||
      path.find(L':') != std::wstring_view::npos) {
    return false;
  }
  std::size_t start = 0;
  while (start <= path.size()) {
    const std::size_t end = path.find_first_of(L"\\/", start);
    const std::wstring_view part =
        path.substr(start, end == std::wstring_view::npos ? path.size() - start : end - start);
    if (part.empty() || part == L"." || part == L"..") return false;
    if (end == std::wstring_view::npos) break;
    start = end + 1;
  }
  return true;
}

core::Status ValidateStart(const HostOperationCallback& callback, AsyncRequestToken* token) {
  if (token == nullptr) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument, L"A request-token output is required");
  }
  *token = {};
  if (!callback) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument, L"An operation callback is required");
  }
  return core::Ok();
}

}  // namespace

struct HostOperationDispatcher::Impl {
  Impl(void* ui_window, unsigned int completion_message, StatePatchSink sink)
      : worker(ui_window, completion_message),
        generation(worker.CreateGeneration()),
        ui_thread_id(GetCurrentThreadId()),
        state_patch_sink(std::move(sink)) {}

  worker::Worker worker;
  std::uint64_t generation = 0;
  unsigned long ui_thread_id = 0;
  StatePatchSink state_patch_sink;
  std::mutex mutex;
  std::map<std::uint64_t, worker::JobHandle> requests;
  std::uint64_t next_token = 1;
  bool invalidated = false;
};

core::Status BuildProcessCommandLine(const ProcessRequest& request, std::wstring* out) {
  if (out == nullptr) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument, L"A command-line output is required");
  }
  out->clear();
  if (request.executable.empty() || ContainsInvalidExecutableCharacter(request.executable)) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument, L"Executable is empty or invalid");
  }
  for (const std::wstring& argument : request.arguments) {
    if (argument.find(L'\0') != std::wstring::npos) {
      return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                       L"Process arguments cannot contain embedded nulls");
    }
  }

  out->push_back(L'\"');
  out->append(request.executable);
  out->push_back(L'\"');
  const std::wstring arguments = BuildArgumentList(request.arguments);
  if (!arguments.empty()) {
    out->push_back(L' ');
    out->append(arguments);
  }
  return core::Ok();
}

HostOperationDispatcher::HostOperationDispatcher(void* ui_window,
                                                 unsigned int completion_message,
                                                 StatePatchSink state_patch_sink)
    : impl_(std::make_unique<Impl>(ui_window, completion_message,
                                  std::move(state_patch_sink))) {}

HostOperationDispatcher::~HostOperationDispatcher() {
  InvalidateGeneration();
  impl_->worker.Shutdown();
}

core::Status HostOperationDispatcher::StartProcess(const ProcessRequest& request,
                                                   HostOperationCallback callback,
                                                   AsyncRequestToken* token) {
  DHEPZ_RETURN_IF_ERROR(ValidateStart(callback, token));
  std::wstring command_line;
  DHEPZ_RETURN_IF_ERROR(BuildProcessCommandLine(request, &command_line));
  if (request.operation == ProcessOperation::Capture && request.timeout_ms == 0) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                     L"Capture timeout must be greater than zero");
  }

  AsyncRequestToken issued;
  {
    std::lock_guard lock(impl_->mutex);
    if (impl_->invalidated) {
      return DHEPZ_ERR(core::ErrorCode::Cancelled, L"Module host generation is invalid");
    }
    issued.value = impl_->next_token++;
  }

  const std::uint64_t generation = impl_->generation;
  const HostOperationKind kind = OperationKind(request.operation);
  worker::JobHandle handle = impl_->worker.Submit(
      [request, command_line = std::move(command_line), issued, generation,
       kind](const std::atomic<bool>& cancelled) {
        auto completion = std::make_shared<HostOperationCompletion>();
        completion->token = issued;
        completion->generation = generation;
        completion->kind = kind;
        if (cancelled.load()) return std::static_pointer_cast<void>(completion);

        if (request.operation == ProcessOperation::Launch) {
          completion->status = process::Launch(request.executable, command_line,
                                               request.working_directory,
                                               process::WindowMode::NewConsole);
        } else if (request.operation == ProcessOperation::ElevatedLaunch) {
          completion->status = process::ShellLaunch(L"runas", request.executable,
                                                    BuildArgumentList(request.arguments),
                                                    request.working_directory);
        } else {
          process::RunResult result;
          completion->status = process::RunCapture(command_line, request.working_directory,
                                                    request.timeout_ms, &result, &cancelled);
          completion->process.exit_code = result.exit_code;
          completion->process.output = std::move(result.output);
          completion->process.timed_out = result.timed_out;
          if (completion->status.ok() && result.timed_out) {
            completion->status =
                core::Err(core::ErrorCode::Cancelled, L"Process capture timed out");
          }
        }
        return std::static_pointer_cast<void>(completion);
      },
      [this, callback = std::move(callback), issued](std::shared_ptr<void> cargo) {
        {
          std::lock_guard lock(impl_->mutex);
          impl_->requests.erase(issued.value);
        }
        callback(*std::static_pointer_cast<HostOperationCompletion>(std::move(cargo)));
      },
      generation);

  {
    std::lock_guard lock(impl_->mutex);
    if (impl_->invalidated) {
      impl_->worker.Cancel(handle);
      return DHEPZ_ERR(core::ErrorCode::Cancelled, L"Module host generation is invalid");
    }
    impl_->requests.emplace(issued.value, std::move(handle));
  }
  *token = issued;
  return core::Ok();
}

core::Status HostOperationDispatcher::StartFolderProbe(const FolderProbeRequest& request,
                                                       HostOperationCallback callback,
                                                       AsyncRequestToken* token) {
  DHEPZ_RETURN_IF_ERROR(ValidateStart(callback, token));
  for (const std::wstring& relative : request.relative_files) {
    if (!SafeRelativeFile(relative)) {
      return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                       L"Folder probes require safe relative file paths");
    }
  }

  AsyncRequestToken issued;
  {
    std::lock_guard lock(impl_->mutex);
    if (impl_->invalidated) {
      return DHEPZ_ERR(core::ErrorCode::Cancelled, L"Module host generation is invalid");
    }
    issued.value = impl_->next_token++;
  }
  const std::uint64_t generation = impl_->generation;
  worker::JobHandle handle = impl_->worker.Submit(
      [request, issued, generation](const std::atomic<bool>& cancelled) {
        auto completion = std::make_shared<HostOperationCompletion>();
        completion->token = issued;
        completion->generation = generation;
        completion->kind = HostOperationKind::FolderProbe;
        if (cancelled.load()) return std::static_pointer_cast<void>(completion);

        completion->folder.normalized_directory = paths::Normalize(request.directory);
        if (completion->folder.normalized_directory.empty()) {
          completion->status =
              core::Err(core::ErrorCode::InvalidArgument, L"Folder path is not absolute");
        } else {
          completion->folder.directory_exists =
              paths::DirectoryExists(completion->folder.normalized_directory);
          if (!completion->folder.directory_exists) {
            completion->status =
                core::Err(core::ErrorCode::NotFound, L"Folder does not exist or is inaccessible");
          }
        }
        for (const std::wstring& relative : request.relative_files) {
          RelativeFilePresence presence;
          presence.relative_path = relative;
          if (completion->folder.directory_exists) {
            presence.present = paths::FileExists(
                paths::Join(completion->folder.normalized_directory, relative));
          }
          completion->folder.files.push_back(std::move(presence));
        }
        return std::static_pointer_cast<void>(completion);
      },
      [this, callback = std::move(callback), issued](std::shared_ptr<void> cargo) {
        {
          std::lock_guard lock(impl_->mutex);
          impl_->requests.erase(issued.value);
        }
        callback(*std::static_pointer_cast<HostOperationCompletion>(std::move(cargo)));
      },
      generation);

  {
    std::lock_guard lock(impl_->mutex);
    if (impl_->invalidated) {
      impl_->worker.Cancel(handle);
      return DHEPZ_ERR(core::ErrorCode::Cancelled, L"Module host generation is invalid");
    }
    impl_->requests.emplace(issued.value, std::move(handle));
  }
  *token = issued;
  return core::Ok();
}

core::Status HostOperationDispatcher::StartAtomicWrite(
    std::wstring path, std::wstring text, HostOperationCallback callback,
    AsyncRequestToken* token) {
  DHEPZ_RETURN_IF_ERROR(ValidateStart(callback, token));
  if (path.empty()) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                     L"An atomic-write target is required");
  }

  AsyncRequestToken issued;
  {
    std::lock_guard lock(impl_->mutex);
    if (impl_->invalidated) {
      return DHEPZ_ERR(core::ErrorCode::Cancelled,
                       L"Module host generation is invalid");
    }
    issued.value = impl_->next_token++;
  }
  const std::uint64_t generation = impl_->generation;
  worker::JobHandle handle = impl_->worker.Submit(
      [path = std::move(path), text = std::move(text), issued,
       generation](const std::atomic<bool>& cancelled) {
        auto completion = std::make_shared<HostOperationCompletion>();
        completion->token = issued;
        completion->generation = generation;
        completion->kind = HostOperationKind::ConfigSave;
        if (!cancelled.load()) {
          completion->status = files::WriteTextAtomic(path, text);
        }
        return std::static_pointer_cast<void>(completion);
      },
      [this, callback = std::move(callback), issued](
          std::shared_ptr<void> cargo) {
        {
          std::lock_guard lock(impl_->mutex);
          impl_->requests.erase(issued.value);
        }
        callback(*std::static_pointer_cast<HostOperationCompletion>(
            std::move(cargo)));
      },
      generation);

  {
    std::lock_guard lock(impl_->mutex);
    if (impl_->invalidated) {
      impl_->worker.Cancel(handle);
      return DHEPZ_ERR(core::ErrorCode::Cancelled,
                       L"Module host generation is invalid");
    }
    impl_->requests.emplace(issued.value, std::move(handle));
  }
  *token = issued;
  return core::Ok();
}

void HostOperationDispatcher::CancelRequest(AsyncRequestToken token) {
  worker::JobHandle handle;
  {
    std::lock_guard lock(impl_->mutex);
    const auto found = impl_->requests.find(token.value);
    if (found == impl_->requests.end()) return;
    handle = found->second;
    impl_->requests.erase(found);
  }
  impl_->worker.Cancel(handle);
}

void HostOperationDispatcher::InvalidateGeneration() {
  std::vector<worker::JobHandle> handles;
  {
    std::lock_guard lock(impl_->mutex);
    if (impl_->invalidated) return;
    impl_->invalidated = true;
    for (const auto& [token, handle] : impl_->requests) {
      (void)token;
      handles.push_back(handle);
    }
    impl_->requests.clear();
  }
  impl_->worker.InvalidateGeneration(impl_->generation);
  for (const worker::JobHandle& handle : handles) impl_->worker.Cancel(handle);
}

core::Status HostOperationDispatcher::PublishStatePatch(const json::Value& patch) {
  if (GetCurrentThreadId() != impl_->ui_thread_id) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                     L"State patches may only be published on the UI thread");
  }
  {
    std::lock_guard lock(impl_->mutex);
    if (impl_->invalidated) {
      return DHEPZ_ERR(core::ErrorCode::Cancelled, L"Module host generation is invalid");
    }
  }
  if (!impl_->state_patch_sink) {
    return DHEPZ_ERR(core::ErrorCode::Unsupported, L"State patch sink is not configured");
  }
  return impl_->state_patch_sink(patch);
}

void HostOperationDispatcher::ReapFinished() { impl_->worker.JoinFinished(); }

std::size_t HostOperationDispatcher::ThreadCount() const { return impl_->worker.ThreadCount(); }

}  // namespace modules
