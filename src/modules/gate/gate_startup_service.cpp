#include "modules/gate/gate_startup_service.h"

#include <windows.h>

#include <atomic>
#include <memory>
#include <string_view>
#include <utility>

#include "platform/files.h"
#include "platform/strings.h"
#include "platform/worker.h"

namespace modules {
namespace {

struct StartupReadResult {
  core::Status status;
  std::wstring text;
};

}  // namespace

class GateStartupService::Impl final {
 public:
  Impl(void* ui_window, unsigned int completion_message)
      : worker(ui_window, completion_message),
        generation(worker.CreateGeneration()) {}

  worker::Worker worker;
  std::uint64_t generation = 0;
};

GateStartupService::GateStartupService(void* ui_window,
                                       unsigned int completion_message)
    : impl_(std::make_unique<Impl>(ui_window, completion_message)) {}

GateStartupService::~GateStartupService() = default;

core::Status GateStartupService::ReadEmbeddedResource(
    std::wstring_view resource_name, std::wstring* out) {
  if (out == nullptr) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                     L"Embedded UI output is required");
  }
  out->clear();
  const HMODULE module = GetModuleHandleW(nullptr);
  const std::wstring name(resource_name);
  const HRSRC resource = FindResourceW(module, name.c_str(), RT_RCDATA);
  if (resource == nullptr) {
    return DHEPZ_ERR(core::ErrorCode::NotFound,
                     L"Embedded UI RCDATA is missing");
  }
  const HGLOBAL loaded = LoadResource(module, resource);
  const DWORD size = SizeofResource(module, resource);
  const void* bytes = loaded != nullptr ? LockResource(loaded) : nullptr;
  if (bytes == nullptr || size == 0) {
    return DHEPZ_ERR(core::ErrorCode::IoError,
                     L"Embedded UI RCDATA could not be loaded");
  }
  return str::FromUtf8(
      std::string_view(static_cast<const char*>(bytes),
                       static_cast<std::size_t>(size)),
      out);
}

core::Status GateStartupService::ReadOverride(std::wstring path,
                                              StartupReadCallback callback) {
  if (path.empty() || !callback) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                     L"Override path and completion are required");
  }
  impl_->worker.Submit(
      [path = std::move(path)](const std::atomic<bool>& cancelled) {
        auto result = std::make_shared<StartupReadResult>();
        if (cancelled.load()) {
          result->status = core::Err(core::ErrorCode::Cancelled,
                                     L"Config override read was cancelled");
        } else {
          result->status = files::ReadText(path, &result->text);
        }
        return std::static_pointer_cast<void>(result);
      },
      [callback = std::move(callback)](std::shared_ptr<void> cargo) {
        const auto result =
            std::static_pointer_cast<StartupReadResult>(std::move(cargo));
        callback(result->status, std::move(result->text));
      },
      impl_->generation);
  return core::Ok();
}

}  // namespace modules
