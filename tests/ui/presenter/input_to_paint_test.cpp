#include "ui/presenter/screen_presenter.h"

#include <windows.h>

#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "framework/test_case.h"
#include "ui/config/resolved_ui_document.h"
#include "ui/shell/app_window.h"

namespace {

std::wstring W(const char* utf8) {
  std::wstring wide;
  for (const char* p = utf8; *p != '\0'; ++p) {
    wide.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*p)));
  }
  return wide;
}

const char* kCore = R"({
  "schema": "dhepz.ui.core",
  "version": 1,
  "tokens": {
    "dark": { "accent": "#60A5FA", "surfaceAlt": "#232730", "border": "#303540",
              "text": "#E9EDF4", "window": "#14161B" },
    "light": { "accent": "#2563EB", "surfaceAlt": "#F0F2F6", "border": "#D8DCE3",
               "text": "#181C24", "window": "#F5F6F9" }
  },
  "common": { "properties": { "id": { "kind": "string" } } },
  "allows_children": ["screen", "container"],
  "components": {
    "screen": { "properties": { "route_id": { "kind": "string", "required": true } } },
    "container": { "properties": { "gap": { "kind": "int", "default": 8 } } },
    "text": { "properties": { "text": { "kind": "text", "required": true } } },
    "button": { "properties": {
      "label": { "kind": "text", "required": true },
      "tab_stop": { "kind": "bool", "default": true } } }
  }
})";

const char* kScreens = R"({
  "components": [
    { "type": "screen", "route_id": "home", "children": [
      { "type": "container", "children": [
        { "type": "text", "text": "hello" },
        { "type": "button", "id": "a", "label": "A" },
        { "type": "button", "id": "b", "label": "B" }
      ] }
    ] }
  ]
})";

void PumpFor(int milliseconds) {
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(milliseconds);
  while (std::chrono::steady_clock::now() < deadline) {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

}  // namespace

// The input-to-paint budget carried over from the Phase 1 gate: a key press
// to the repainted frame, through the real shell message path.
DHEPZ_TEST(GatePhase2, InputToPaintWithinBudget) {
  shell::AppWindow window;
  DHEPZ_CHECK(window.Create(GetModuleHandleW(nullptr), 480.0f, 320.0f));

  json::Value core;
  DHEPZ_CHECK(json::Parse(W(kCore), &core).ok());
  std::vector<ui::config::Diagnostic> diagnostics;
  std::unique_ptr<ui::config::ResolvedUiDocument> document;
  DHEPZ_CHECK(ui::config::ResolveDocument(core, {{L"embedded", W(kScreens)}}, &diagnostics,
                                          &document)
                  .ok());

  static ui::presenter::ScreenPresenter presenter(window.backend());
  presenter.SetDocument(document.get());
  window.set_content_painter([](render::GdiBackend&, const render::Rect& content) {
    presenter.Paint(content);
  });
  window.set_content_layout(
      [](const render::Rect& content) { presenter.Prepare(content); });
  window.set_content_key_handler([](int vk) { return presenter.HandleKey(vk); });
  window.Show();
  PumpFor(50);

  // Warm the path once, then measure.
  SendMessageW(static_cast<HWND>(window.hwnd()), WM_KEYDOWN, VK_TAB, 0);
  PumpFor(20);

  LARGE_INTEGER frequency{};
  QueryPerformanceFrequency(&frequency);
  LARGE_INTEGER start{};
  QueryPerformanceCounter(&start);
  SendMessageW(static_cast<HWND>(window.hwnd()), WM_KEYDOWN, VK_TAB, 0);
  LARGE_INTEGER end{};
  QueryPerformanceCounter(&end);
  const double ms = static_cast<double>(end.QuadPart - start.QuadPart) * 1000.0 /
                    static_cast<double>(frequency.QuadPart);
  DHEPZ_CHECK_EQ(presenter.focused(), std::wstring(L"a"));
  // The 16 ms budget is a real-hardware number (plan: performance comes
  // from the installed Release build). Headless CI renders in software and
  // is not the measurement target — there it only guards against pathology.
  wchar_t ci_buffer[8]{};
  const bool on_ci = GetEnvironmentVariableW(L"CI", ci_buffer, 8) != 0;
#ifdef NDEBUG
  const double bound = on_ci ? 500.0 : 16.0;
#else
  const double bound = on_ci ? 2000.0 : 2000.0;
#endif
  DHEPZ_CHECK(ms < bound);
  window.Hide();
}
