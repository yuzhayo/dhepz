#include "parent/logic/module_registry.h"

#include <atomic>
#include <cstdio>
#include <ctime>
#include <memory>
#include <string>
#include <utility>

#include "modules/opencode/opencode_resources.h"
#include "modules/opencode/opencode_sessions.h"
#include "platform/files.h"
#include "platform/paths.h"

// The opencode tab. Its one button snapshots the opencode session list into
// JSON through the official CLI endpoint (`opencode api get /api/session`) on
// a worker thread, and writes it under the state directory — the one location
// that survives every install and update. Nothing here runs while idle.
namespace modules::opencode {
namespace {

// One page covers any realistic install (the API's own page-size cap; the
// response comes back complete at this limit), so no cursor loop yet.
constexpr wchar_t kSessionsEndpoint[] = L"/api/session?limit=10000";
constexpr wchar_t kSessionsFileName[] = L"sessions.json";

void Add(ui::application::UiPatch* patch, std::wstring path,
         ui::application::UiValue value) {
  patch->changes.push_back({std::move(path), std::move(value)});
}

// Status messages carry an automatic " [file:line]" suffix for traces; the
// status line is for a human, so the suffix comes off before display.
//
// The line paints as one non-wrapping run inside 368 px (see
// GdiBackend::DrawTextRun), so anything longer would lose both of its ends to
// clipping. Truncation keeps the start — the readable part — visible.
std::wstring UserMessage(const core::Status& status) {
  if (status.ok()) return {};
  std::wstring message = status.Message();
  const std::size_t context = message.find(L" [");
  if (context != std::wstring::npos) message.resize(context);
  constexpr std::size_t kDisplayLimit = 59;
  if (message.size() > kDisplayLimit) {
    message.resize(kDisplayLimit - 3);
    message.append(L"...");
  }
  return message;
}

std::wstring NowText() {
  const std::time_t now = std::time(nullptr);
  std::tm local{};
  if (localtime_s(&local, &now) != 0) return {};
  wchar_t buffer[20];
  swprintf_s(buffer, L"%04d-%02d-%02d %02d:%02d:%02d", local.tm_year + 1900,
             local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min,
             local.tm_sec);
  return buffer;
}

// RunProcess merges stderr into the captured text, and opencode reports its
// failures there. The first non-empty line is the useful part of that report.
std::wstring FirstLine(std::wstring_view text) {
  std::size_t start = 0;
  while (start < text.size()) {
    const std::size_t end = text.find(L'\n', start);
    const std::size_t stop = end == std::wstring_view::npos ? text.size() : end;
    std::wstring line(text.substr(start, stop - start));
    while (!line.empty() && (line.back() == L'\r' || line.back() == L' ')) {
      line.pop_back();
    }
    if (!line.empty()) return line;
    if (end == std::wstring_view::npos) break;
    start = end + 1;
  }
  return {};
}

core::Status WriteSessionsExport(const BackgroundCapabilities& capabilities,
                                 const std::atomic<bool>& cancelled,
                                 std::wstring* summary) {
  // cmd.exe is the launcher because opencode on PATH is the npm shim
  // (opencode.cmd), which CreateProcess cannot run directly. Every argument
  // here is quoting-safe as-is: no spaces, no cmd metacharacters.
  ProcessRequest request;
  request.executable = L"cmd.exe";
  request.arguments = {L"/d", L"/c", L"opencode", L"api", L"get",
                       kSessionsEndpoint};
  request.hidden = true;
  std::wstring output;
  const core::Status ran = capabilities.RunProcess(request, &output);
  if (!ran.ok()) {
    const std::wstring detail = FirstLine(output);
    return DHEPZ_ERR(ran.Code(),
                     detail.empty() ? ran.Message() : L"opencode: " + detail);
  }
  if (cancelled.load()) {
    return DHEPZ_ERR(core::ErrorCode::Cancelled, L"session export cancelled");
  }

  std::wstring document;
  std::size_t total = 0;
  DHEPZ_RETURN_IF_ERROR(
      BuildSessionsDocument(output, NowText(), &document, &total));

  const std::wstring path =
      paths::Join(paths::Join(paths::StateDir(), L"opencode"), kSessionsFileName);
  DHEPZ_RETURN_IF_ERROR(files::WriteTextAtomic(path, document));
  if (summary != nullptr) {
    // Short on purpose: the status line shows one 368 px run, so the full
    // absolute path would be unreadable. "app data\state" is where LO finds
    // the file. The absolute path is one `path` variable away for debugging.
    *summary = L"Saved " + std::to_wstring(total) +
               L" sessions to app data\\state\\opencode\\" + kSessionsFileName;
  }
  return core::Ok();
}

class OpencodeController final : public ModuleController {
 public:
  ui::application::UiPatch InitialState(const ModuleHost& host) const override {
    (void)host;
    ui::application::UiPatch patch;
    Add(&patch, L"opencode.status", std::wstring{});
    return patch;
  }

  core::Status Bind(ModuleHost* host,
                    ui::application::UiActionRegistry* actions) override {
    if (host == nullptr || actions == nullptr) {
      return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                       L"opencode module requires its parent contract");
    }
    host_ = host;
    if (!actions->Register(L"opencode.fetch_sessions",
                           [this](const auto&, const auto&) {
      // The summary crosses from the worker thread back to the completion on
      // the UI thread; the shared pointer owns it for exactly that window.
      const auto summary = std::make_shared<std::wstring>();
      host_->RunBackground(
          [summary](const BackgroundCapabilities& capabilities,
                    const std::atomic<bool>& cancelled) {
            return WriteSessionsExport(capabilities, cancelled, summary.get());
          },
          [this, summary](const core::Status& status) {
            ui::application::UiPatch done;
            Add(&done, L"opencode.status",
                status.ok() ? *summary : UserMessage(status));
            host_->Publish(std::move(done));
          });
      ui::application::UiPatch pending;
      Add(&pending, L"opencode.status",
          std::wstring(L"Fetching opencode sessions..."));
      return pending;
    })) {
      return DHEPZ_ERR(core::ErrorCode::AlreadyExists,
                       L"opencode action registration failed");
    }
    return core::Ok();
  }

 private:
  ModuleHost* host_ = nullptr;
};

std::unique_ptr<ModuleController> CreateOpencodeController() {
  return std::make_unique<OpencodeController>();
}

const ModuleDescriptor kOpencodeDescriptor{
    L"opencode", L"opencode", L"modules/opencode/opencode.json",
    IDR_OPENCODE_SCREEN_JSON, &CreateOpencodeController};
const ModuleRegistrar kOpencodeRegistration(&kOpencodeDescriptor);

}  // namespace
}  // namespace modules::opencode
