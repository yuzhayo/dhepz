#include "modules/terminal/wsl.h"

#include <algorithm>
#include <cwctype>
#include <utility>

namespace terminal {
namespace {

std::wstring TrimLine(std::wstring line) {
  line.erase(std::remove(line.begin(), line.end(), L'\0'), line.end());
  while (!line.empty() && (std::iswspace(line.back()) || line.back() == L'\uFEFF')) {
    line.pop_back();
  }
  std::size_t first = 0;
  while (first < line.size() && (std::iswspace(line[first]) || line[first] == L'\uFEFF')) {
    ++first;
  }
  line.erase(0, first);
  return line;
}

bool IsRecognizedHeader(const std::wstring& line) {
  return line == L"Windows Subsystem for Linux Distributions:" ||
         line == L"Windows Subsystem for Linux Distributions";
}

json::Value StringArray(const std::vector<std::wstring>& values) {
  json::Value array = json::Value::Array();
  for (const std::wstring& value : values) {
    array.Append(json::Value::String(value));
  }
  return array;
}

}  // namespace

std::vector<std::wstring> ParseWslListOutput(std::wstring_view output) {
  std::vector<std::wstring> distros;
  std::wstring line;
  const auto flush = [&] {
    std::wstring name = TrimLine(line);
    if (name.empty() || IsRecognizedHeader(name)) return;

    constexpr std::wstring_view marker = L" (Default)";
    if (name.size() >= marker.size() &&
        name.compare(name.size() - marker.size(), marker.size(), marker) == 0) {
      name.erase(name.size() - marker.size());
      name = TrimLine(std::move(name));
    }
    if (!name.empty()) distros.push_back(std::move(name));
  };
  for (const wchar_t c : output) {
    if (c == L'\n') {
      flush();
      line.clear();
    } else {
      line.push_back(c);
    }
  }
  if (!line.empty()) flush();
  return distros;
}

void WslSession::WriteDefaults(json::Value* patch) const {
  if (patch == nullptr) return;
  patch->Set(L"wsl_distros", StringArray(distros_));
  patch->Set(L"wsl_distro", distros_.empty()
                                  ? json::Value::Null()
                                  : json::Value::String(distros_.front()));
}

core::Status WslSession::Bind(modules::ModuleHost& host) {
  host_ = &host;
  ++generation_;
  return cached_ ? core::Ok() : Refresh(nullptr);
}

core::Status WslSession::Refresh(json::Value* immediate_patch) {
  if (host_ == nullptr) {
    return core::Err(core::ErrorCode::InvalidArgument,
                     L"WSL session is not bound");
  }
  if (token_) {
    host_->CancelRequest(token_);
    token_ = {};
  }
  const std::uint64_t refresh = ++refresh_;
  const std::uint64_t generation = generation_;

  modules::ProcessRequest request;
  request.operation = modules::ProcessOperation::Capture;
  request.executable = L"wsl.exe";
  request.arguments = {L"-l", L"-q"};
  request.timeout_ms = 10000;

  if (immediate_patch != nullptr) {
    *immediate_patch = json::Value::Object();
    immediate_patch->Set(L"busy", json::Value::Bool(true));
    immediate_patch->Set(L"status",
                         json::Value::String(L"Refreshing WSL distros..."));
  }

  modules::AsyncRequestToken issued;
  const core::Status started = host_->StartProcess(
      request,
      [this, generation, refresh](
          const modules::HostOperationCompletion& completion) {
        if (host_ == nullptr || generation != generation_ ||
            refresh != refresh_ || completion.token != token_ ||
            completion.kind != modules::HostOperationKind::Capture) {
          return;
        }
        token_ = {};

        core::Status result = completion.status;
        if (result.ok() && completion.process.timed_out) {
          result = core::Err(core::ErrorCode::Cancelled,
                             L"WSL enumeration timed out");
        } else if (result.ok() && completion.process.exit_code != 0) {
          result = core::Err(
              core::ErrorCode::IoError,
              L"WSL enumeration exited with code " +
                  std::to_wstring(completion.process.exit_code));
        }

        json::Value patch = json::Value::Object();
        patch.Set(L"busy", json::Value::Bool(false));
        if (!result.ok()) {
          WriteDefaults(&patch);
          patch.Set(L"status", json::Value::String(result.Message()));
          host_->ReportStatus(result);
        } else {
          distros_ = ParseWslListOutput(completion.process.output);
          cached_ = true;
          WriteDefaults(&patch);
          patch.Set(L"status", json::Value::String(
              distros_.empty() ? L"No WSL distros found"
                               : L"WSL distros ready"));
          host_->ReportStatus(core::Ok());
        }
        const core::Status published = host_->PublishStatePatch(patch);
        if (!published.ok()) host_->ReportStatus(published);
      },
      &issued);
  if (!started.ok()) {
    if (immediate_patch != nullptr) {
      immediate_patch->Set(L"busy", json::Value::Bool(false));
      immediate_patch->Set(L"status", json::Value::String(started.Message()));
    } else {
      json::Value failed = json::Value::Object();
      failed.Set(L"busy", json::Value::Bool(false));
      failed.Set(L"status", json::Value::String(started.Message()));
      const core::Status published = host_->PublishStatePatch(failed);
      if (!published.ok()) host_->ReportStatus(published);
    }
    return started;
  }
  token_ = issued;
  return core::Ok();
}

void WslSession::Release() {
  ++generation_;
  if (host_ != nullptr && token_) host_->CancelRequest(token_);
  token_ = {};
  host_ = nullptr;
}

}  // namespace terminal
