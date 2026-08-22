#include "platform/tray_process.h"

#include <windows.h>
#include <shellapi.h>
#include <windowsx.h>

#include "resource.h"

namespace tray {
namespace {

constexpr UINT kTrayCallbackMessage = WM_APP + 1;
constexpr UINT kTrayIconId = 1;
constexpr UINT kExitCommand = 1;

std::wstring ProcessStem() {
  std::wstring path(32768, L'\0');
  const DWORD length =
      GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
  if (length == 0 || static_cast<std::size_t>(length) >= path.size()) return L"dhepz";
  path.resize(length);
  const std::size_t separator = path.find_last_of(L"\\/");
  std::wstring name = separator == std::wstring::npos ? path : path.substr(separator + 1);
  const std::size_t extension = name.find_last_of(L'.');
  if (extension != std::wstring::npos) name.resize(extension);
  return name.empty() ? std::wstring(L"dhepz") : name;
}

}  // namespace

TrayProcess::TrayProcess() noexcept = default;

TrayProcess::~TrayProcess() { Shutdown(); }

StartResult TrayProcess::Start(void* instance) noexcept {
  instance_ = instance;
  const std::wstring key = ProcessStem();
  class_name_ = key + L".InfrastructureWindow";
  launch_message_ = RegisterWindowMessageW((key + L".CreateWindow").c_str());
  if (launch_message_ == 0) return StartResult::SingleInstanceFailed;

  const std::wstring mutex_name = L"Local\\" + key + L".SingleInstance";
  HANDLE mutex = CreateMutexW(nullptr, FALSE, mutex_name.c_str());
  if (mutex == nullptr) return StartResult::SingleInstanceFailed;
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    for (int attempt = 0; attempt < 100; ++attempt) {
      const HWND owner = FindWindowW(class_name_.c_str(), nullptr);
      if (owner != nullptr && PostMessageW(owner, launch_message_, 0, 0)) {
        CloseHandle(mutex);
        return StartResult::ExistingOwnerNotified;
      }
      Sleep(10);
    }
    CloseHandle(mutex);
    return StartResult::ExistingOwnerNotificationFailed;
  }
  instance_mutex_ = mutex;

  WNDCLASSEXW window_class{};
  window_class.cbSize = sizeof(window_class);
  // Signature matches WNDPROC exactly on x64 (the only platform); the types
  // are spelled out because the header keeps windows.h out.
  window_class.lpfnWndProc = reinterpret_cast<WNDPROC>(&TrayProcess::WindowProcedure);
  window_class.hInstance = static_cast<HINSTANCE>(instance);
  window_class.lpszClassName = class_name_.c_str();
  if (!RegisterClassExW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    return StartResult::WindowClassFailed;
  }

  taskbar_created_message_ = RegisterWindowMessageW(L"TaskbarCreated");
  if (taskbar_created_message_ == 0) {
    return StartResult::TaskbarMessageFailed;
  }

  // A real top-level WS_POPUP, never HWND_MESSAGE: message-only windows do
  // not receive the TaskbarCreated broadcast, and the tray icon would be
  // lost forever after an Explorer restart. See the header.
  window_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, class_name_.c_str(), nullptr,
                            WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                            static_cast<HINSTANCE>(instance), this);
  if (window_ == nullptr) {
    return StartResult::WindowCreateFailed;
  }
  return StartResult::Ok;
}

bool TrayProcess::InstallTray() noexcept {
  if (window_ == nullptr || tray_icon_added_) {
    return tray_icon_added_;
  }
  AddTrayIcon();
  return tray_icon_added_;
}

void TrayProcess::set_launch_handler(std::function<void()> handler) {
  launch_handler_ = std::move(handler);
}

int TrayProcess::Run() noexcept {
  MSG message{};
  // Blocks in GetMessageW. No timers, no polling, no idle processing: the
  // loop wakes only when the OS has something to deliver (G1).
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  return static_cast<int>(message.wParam);
}

void TrayProcess::Shutdown() noexcept {
  if (tray_icon_added_ && window_ != nullptr) {
    NOTIFYICONDATAW icon{};
    icon.cbSize = sizeof(icon);
    icon.hWnd = static_cast<HWND>(window_);
    icon.uID = kTrayIconId;
    Shell_NotifyIconW(NIM_DELETE, &icon);
    tray_icon_added_ = false;
  }
  if (window_ != nullptr) {
    DestroyWindow(static_cast<HWND>(window_));
    window_ = nullptr;
  }
  if (instance_mutex_ != nullptr) {
    CloseHandle(static_cast<HANDLE>(instance_mutex_));
    instance_mutex_ = nullptr;
  }
}

long long __stdcall TrayProcess::WindowProcedure(void* window, unsigned int message,
                                                 unsigned long long wparam, long long lparam) {
  HWND hwnd = static_cast<HWND>(window);
  TrayProcess* owner = reinterpret_cast<TrayProcess*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
    owner = static_cast<TrayProcess*>(create->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(owner));
    if (owner != nullptr) {
      owner->window_ = hwnd;
    }
  }
  if (owner != nullptr) {
    return owner->HandleMessage(message, wparam, lparam);
  }
  return DefWindowProcW(hwnd, message, static_cast<WPARAM>(wparam), static_cast<LPARAM>(lparam));
}

long long TrayProcess::HandleMessage(unsigned int message, unsigned long long wparam,
                                     long long lparam) {
  HWND hwnd = static_cast<HWND>(window_);
  if (launch_message_ != 0 && message == launch_message_) {
    if (launch_handler_) launch_handler_();
    return 0;
  }
  if (taskbar_created_message_ != 0 && message == taskbar_created_message_) {
    // Explorer came back: its icon list is empty, so the icon must be
    // re-added or it stays gone until the next process start.
    if (tray_icon_added_) {
      tray_icon_added_ = false;
      AddTrayIcon();
    }
    return 0;
  }
  if (message == kTrayCallbackMessage) {
    HandleTrayCallback(wparam, lparam);
    return 0;
  }
  switch (message) {
    case WM_DESTROY:
      window_ = nullptr;
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProcW(hwnd, message, static_cast<WPARAM>(wparam),
                            static_cast<LPARAM>(lparam));
  }
}

void TrayProcess::HandleTrayCallback(unsigned long long wparam, long long lparam) {
  // NOTIFYICON_VERSION_4 contract: the event is LOWORD(lParam), the icon ID
  // is HIWORD(lParam). wParam is NOT a reliable carrier for the icon ID —
  // measured on Windows 11 it arrives as an unrelated varying value even
  // after a successful NIM_SETVERSION — so only the lParam pair is decoded.
  // This layout also decodes legacy callbacks correctly: the plain message
  // sits in LOWORD and HIWORD is zero.
  (void)wparam;
  const DWORD data = static_cast<DWORD>(lparam);
  const UINT event = LOWORD(data);
  const UINT icon_id = HIWORD(data);
  if (icon_id != 0 && icon_id != kTrayIconId) {
    return;
  }
  if (event == WM_CONTEXTMENU || event == WM_RBUTTONUP) {
    ShowMenu();
    return;
  }
  if ((event == NIN_SELECT || event == NIN_KEYSELECT || event == WM_LBUTTONUP) &&
      launch_handler_) {
    launch_handler_();
  }
}

void TrayProcess::ShowMenu() {
  POINT point{};
  // WM_CONTEXTMENU carries the position (keyboard invocation); everywhere
  // else the cursor is the source of truth.
  GetCursorPos(&point);

  const HMENU menu = CreatePopupMenu();
  if (menu == nullptr) {
    return;
  }
  AppendMenuW(menu, MF_STRING, kExitCommand, L"Exit");

  // SetForegroundWindow lets the menu dismiss on the next click; the posted
  // WM_NULL is the documented companion to TPM_RETURNCMD for tray menus.
  SetForegroundWindow(static_cast<HWND>(window_));
  const UINT selected = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                                         point.x, point.y, static_cast<HWND>(window_), nullptr);
  DestroyMenu(menu);
  PostMessageW(static_cast<HWND>(window_), WM_NULL, 0, 0);

  if (selected == kExitCommand) {
    Shutdown();  // WM_DESTROY posts the quit that ends Run().
  }
}

void TrayProcess::AddTrayIcon() noexcept {
  NOTIFYICONDATAW icon{};
  icon.cbSize = sizeof(icon);
  icon.hWnd = static_cast<HWND>(window_);
  icon.uID = kTrayIconId;
  icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
  icon.uCallbackMessage = kTrayCallbackMessage;
  icon.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP_ICON));
  wcscpy_s(icon.szTip, L"dhepz");
  if (icon.hIcon == nullptr || !Shell_NotifyIconW(NIM_ADD, &icon)) {
    tray_icon_added_ = false;
    return;
  }
  // After the add, not before: this call is what switches the callback to
  // the v4 layout and enables the modern NIN_* semantics.
  icon.uVersion = NOTIFYICON_VERSION_4;
  Shell_NotifyIconW(NIM_SETVERSION, &icon);
  tray_icon_added_ = true;
}

}  // namespace tray
