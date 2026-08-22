#include "ui/components/input/input_component.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>
#include <windows.h>

#include "ui/components/component_helpers.h"

namespace ui::components {
namespace {
constexpr float kSuggestionRowHeight = 30.0f;
constexpr UINT kMenuCut = 1;
constexpr UINT kMenuCopy = 2;
constexpr UINT kMenuPaste = 3;
constexpr UINT kMenuSelectAll = 4;

struct Selection {
  std::size_t caret = 0;
  std::size_t anchor = 0;
  std::size_t begin() const { return std::min(caret, anchor); }
  std::size_t end() const { return std::max(caret, anchor); }
  bool empty() const { return caret == anchor; }
};

render::Size Measure(const config::ComponentNode& node, render::RenderBackend& backend,
                     const application::UiState&, float max_width) {
  // Layout is outside the paint scope, so this warms the exact font metrics
  // later used by the backend's editable-text drawing primitive.
  backend.MeasureText(L"Mg", {}, 0.0f);
  const float width = std::min(max_width, static_cast<float>(node.GetInt(L"width", 240)));
  const float default_height = node.GetString(L"mode") == L"multiline" ? 96.0f : 34.0f;
  return {width, static_cast<float>(node.GetInt(L"height",
                                               static_cast<long long>(default_height)))};
}

std::wstring Value(const config::ComponentNode& node, const application::UiState& state) {
  return BoundText(node, L"value_binding", state);
}

std::wstring EditorKey(const config::ComponentNode& node, std::wstring_view field) {
  std::wstring key = L"__ui.input.";
  key.append(node.id().empty() ? node.GetString(L"value_binding") : node.id());
  key.push_back(L'.');
  key.append(field);
  return key;
}

Selection ReadSelection(const config::ComponentNode& node,
                        const application::UiState& state, std::size_t length) {
  const std::size_t caret = static_cast<std::size_t>(
      std::clamp(state.Integer(EditorKey(node, L"caret"), static_cast<long long>(length)),
                 0LL, static_cast<long long>(length)));
  const std::size_t anchor = static_cast<std::size_t>(
      std::clamp(state.Integer(EditorKey(node, L"anchor"), static_cast<long long>(caret)),
                 0LL, static_cast<long long>(length)));
  return {caret, anchor};
}

void AddSelection(application::UiPatch* patch, const config::ComponentNode& node,
                  Selection selection) {
  patch->changes.push_back(
      {EditorKey(node, L"caret"), static_cast<long long>(selection.caret)});
  patch->changes.push_back(
      {EditorKey(node, L"anchor"), static_cast<long long>(selection.anchor)});
}

ComponentResult Select(const config::ComponentNode& node, Selection selection) {
  ComponentResult result;
  result.handled = true;
  AddSelection(&result.patch, node, selection);
  return result;
}

ComponentResult Changed(const config::ComponentNode& node, std::wstring value,
                        Selection selection) {
  ComponentResult result = BindingResult(node, L"value_binding", std::move(value), true);
  AddSelection(&result.patch, node, selection);
  return result;
}

bool ClipboardHasText() { return IsClipboardFormatAvailable(CF_UNICODETEXT) != FALSE; }

std::wstring ClipboardText() {
  if (!OpenClipboard(nullptr)) return {};
  const HANDLE data = GetClipboardData(CF_UNICODETEXT);
  const wchar_t* text = data != nullptr
                            ? static_cast<const wchar_t*>(GlobalLock(data))
                            : nullptr;
  const std::wstring value = text != nullptr ? std::wstring(text) : std::wstring{};
  if (text != nullptr) GlobalUnlock(data);
  CloseClipboard();
  return value;
}

bool PutClipboardText(std::wstring_view value) {
  if (!OpenClipboard(nullptr)) return false;
  if (!EmptyClipboard()) {
    CloseClipboard();
    return false;
  }
  const SIZE_T bytes = (value.size() + 1) * sizeof(wchar_t);
  HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
  if (memory == nullptr) {
    CloseClipboard();
    return false;
  }
  wchar_t* destination = static_cast<wchar_t*>(GlobalLock(memory));
  if (destination == nullptr) {
    GlobalFree(memory);
    CloseClipboard();
    return false;
  }
  std::copy(value.begin(), value.end(), destination);
  destination[value.size()] = L'\0';
  GlobalUnlock(memory);
  if (SetClipboardData(CF_UNICODETEXT, memory) == nullptr) {
    GlobalFree(memory);
    CloseClipboard();
    return false;
  }
  CloseClipboard();
  return true;
}

ComponentResult Copy(const config::ComponentNode& node,
                     const application::UiState& state, bool cut) {
  if (node.GetBool(L"password")) return {};
  std::wstring value = Value(node, state);
  const Selection selection = ReadSelection(node, state, value.size());
  const std::wstring_view selected = std::wstring_view(value).substr(
      selection.begin(), selection.end() - selection.begin());
  if (selection.empty() || !PutClipboardText(selected)) return {};
  if (!cut || node.GetBool(L"read_only")) {
    ComponentResult result;
    result.handled = true;
    return result;
  }
  value.erase(selection.begin(), selection.end() - selection.begin());
  return Changed(node, std::move(value), {selection.begin(), selection.begin()});
}

ComponentResult Paste(const config::ComponentNode& node,
                      const application::UiState& state) {
  if (node.GetBool(L"read_only")) return {};
  std::wstring inserted = ClipboardText();
  if (inserted.empty()) return {};
  if (node.GetString(L"mode") != L"multiline") {
    inserted.erase(std::remove_if(inserted.begin(), inserted.end(), [](wchar_t value) {
                     return value == L'\r' || value == L'\n';
                   }),
                   inserted.end());
  }
  std::wstring value = Value(node, state);
  const Selection selection = ReadSelection(node, state, value.size());
  const std::size_t maximum = static_cast<std::size_t>(node.GetInt(L"maximum_length", 4096));
  const std::size_t retained = value.size() - (selection.end() - selection.begin());
  if (retained >= maximum) inserted.clear();
  if (inserted.size() > maximum - std::min(maximum, retained)) {
    inserted.resize(maximum - std::min(maximum, retained));
  }
  value.replace(selection.begin(), selection.end() - selection.begin(), inserted);
  const std::size_t caret = selection.begin() + inserted.size();
  return Changed(node, std::move(value), {caret, caret});
}

render::Rect PopupBounds(const config::ComponentNode& node, const render::Rect& anchor,
                         render::Size viewport, std::size_t count) {
  const std::size_t maximum = static_cast<std::size_t>(
      std::max(1LL, node.GetInt(L"maximum_visible_items", 6)));
  const float height = kSuggestionRowHeight * static_cast<float>(std::min(count, maximum));
  float y = anchor.bottom();
  if (y + height > viewport.height) y = std::max(0.0f, anchor.y - height);
  return {anchor.x, y, anchor.width, height};
}

const std::vector<std::wstring>* Suggestions(const config::ComponentNode& node,
                                             const application::UiState& state) {
  return BoundStrings(node, L"suggestions_binding", state);
}

bool HasOverlay(const config::ComponentNode& node,
                const application::UiState& state) {
  const std::vector<std::wstring>* items = Suggestions(node, state);
  return items != nullptr && !items->empty();
}

void Paint(const config::ComponentNode& node, const render::Rect& bounds,
           const ComponentVisualState& visual, const ComponentPalette& palette,
           const application::UiState& state, render::RenderBackend& backend) {
  const render::CornerRadius radius = CornerRadiusFor(node);
  backend.FillRoundedRect(bounds, radius, palette.control);
  backend.StrokeRoundedRect(bounds, radius,
                            visual.focused ? palette.focus : palette.border,
                            visual.focused ? 2.0f : 1.0f);
  std::wstring value = Value(node, state);
  const bool placeholder = value.empty();
  if (placeholder) value = node.GetString(L"placeholder");
  if (!placeholder && node.GetBool(L"password")) value.assign(value.size(), L'•');

  const render::TextStyle style{};
  const render::Rect text_bounds{bounds.x + 9.0f, bounds.y + 2.0f,
                                 std::max(0.0f, bounds.width - 18.0f),
                                 std::max(0.0f, bounds.height - 4.0f)};
  const std::wstring align = node.GetString(L"horizontal_align");
  const render::TextAlign text_align = align == L"center"
                                           ? render::TextAlign::Center
                                           : align == L"end" ? render::TextAlign::Right
                                                             : render::TextAlign::Left;
  if (placeholder || !visual.focused) {
    backend.DrawTextRun(value, text_bounds, style,
                        placeholder ? palette.border : palette.text, text_align,
                        node.GetString(L"mode") == L"multiline"
                            ? render::VerticalAlign::Top
                            : render::VerticalAlign::Middle);
    return;
  }

  const Selection selection = ReadSelection(node, state, value.size());
  backend.DrawEditableTextRun(
      value, text_bounds, style, palette.text,
      {selection.begin(), selection.end(), selection.caret,
       palette.control_pressed, palette.text});
}

ComponentResult Pointer(const config::ComponentNode& node,
                        const application::UiState& state, render::Point point,
                        const render::Rect& bounds, render::RenderBackend& backend) {
  const std::wstring value = Value(node, state);
  if (value.empty()) return Select(node, {0, 0});

  const render::TextStyle style{};
  const render::Rect text_bounds{bounds.x + 9.0f, bounds.y + 2.0f,
                                 std::max(0.0f, bounds.width - 18.0f),
                                 std::max(0.0f, bounds.height - 4.0f)};
  const Selection current = ReadSelection(node, state, value.size());
  const float current_advance = backend.MeasureText(
      std::wstring_view(value).substr(0, current.caret), style, 0.0f).width;
  const float offset = current_advance > text_bounds.width - 2.0f
                           ? text_bounds.width - current_advance - 2.0f
                           : 0.0f;
  const float target = std::max(0.0f, point.x - text_bounds.x - offset);

  std::size_t low = 0;
  std::size_t high = value.size();
  while (low < high) {
    const std::size_t middle = low + (high - low) / 2;
    const float edge = backend.MeasureText(
        std::wstring_view(value).substr(0, middle + 1), style, 0.0f).width;
    if (edge < target) {
      low = middle + 1;
    } else {
      high = middle;
    }
  }
  std::size_t caret = low;
  if (caret < value.size()) {
    const float left = backend.MeasureText(
        std::wstring_view(value).substr(0, caret), style, 0.0f).width;
    const float right = backend.MeasureText(
        std::wstring_view(value).substr(0, caret + 1), style, 0.0f).width;
    if (target >= left + (right - left) / 2.0f) ++caret;
  }
  return Select(node, {caret, caret});
}

ComponentResult DoubleClick(const config::ComponentNode& node,
                            const application::UiState& state) {
  const std::size_t end = Value(node, state).size();
  return Select(node, {end, 0});
}

ComponentResult Key(const config::ComponentNode& node, const application::UiState& state,
                    int key) {
  std::wstring value = Value(node, state);
  Selection selection = ReadSelection(node, state, value.size());
  const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
  const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
  if (control && key == 'A') return Select(node, {value.size(), 0});
  if (control && key == 'C') return Copy(node, state, false);
  if (control && key == 'X') return Copy(node, state, true);
  if (control && key == 'V') return Paste(node, state);
  if (key == VK_LEFT || key == VK_RIGHT || key == VK_HOME || key == VK_END) {
    std::size_t next = selection.caret;
    if (key == VK_HOME) next = 0;
    if (key == VK_END) next = value.size();
    if (key == VK_LEFT) {
      next = !shift && !selection.empty() ? selection.begin()
                                          : (selection.caret == 0 ? 0 : selection.caret - 1);
    }
    if (key == VK_RIGHT) {
      next = !shift && !selection.empty()
                 ? selection.end()
                 : std::min(value.size(), selection.caret + 1);
    }
    return Select(node, {next, shift ? selection.anchor : next});
  }
  if (node.GetBool(L"read_only")) return {};
  if (key == VK_BACK || key == VK_DELETE) {
    if (selection.empty()) {
      if (key == VK_BACK && selection.caret > 0) {
        selection.anchor = selection.caret - 1;
      } else if (key == VK_DELETE && selection.caret < value.size()) {
        selection.anchor = selection.caret + 1;
      } else {
        ComponentResult handled;
        handled.handled = true;
        return handled;
      }
    }
    const std::size_t begin = selection.begin();
    value.erase(begin, selection.end() - begin);
    return Changed(node, std::move(value), {begin, begin});
  }
  if (key == VK_RETURN && node.GetString(L"mode") == L"multiline") {
    const std::size_t maximum = static_cast<std::size_t>(node.GetInt(L"maximum_length", 4096));
    if (value.size() - (selection.end() - selection.begin()) >= maximum) return {};
    value.replace(selection.begin(), selection.end() - selection.begin(), 1, L'\n');
    const std::size_t caret = selection.begin() + 1;
    return Changed(node, std::move(value), {caret, caret});
  }
  if (key == VK_RETURN || key == VK_SPACE) {
    ComponentResult handled;
    handled.handled = true;
    return handled;
  }
  return {};
}

ComponentResult TextInput(const config::ComponentNode& node,
                          const application::UiState& state, wchar_t character) {
  if (node.GetBool(L"read_only") || character < L' ' || character == 0x7F) return {};
  std::wstring value = Value(node, state);
  const Selection selection = ReadSelection(node, state, value.size());
  const std::size_t maximum = static_cast<std::size_t>(node.GetInt(L"maximum_length", 4096));
  if (value.size() - (selection.end() - selection.begin()) >= maximum) return {};
  value.replace(selection.begin(), selection.end() - selection.begin(), 1, character);
  const std::size_t caret = selection.begin() + 1;
  return Changed(node, std::move(value), {caret, caret});
}

void PaintOverlay(const config::ComponentNode& node, const render::Rect& anchor,
                  render::Size viewport, const ComponentPalette& palette,
                  const application::UiState& state, render::RenderBackend& backend) {
  const std::vector<std::wstring>* items = Suggestions(node, state);
  if (items == nullptr || items->empty()) return;
  const render::Rect popup = PopupBounds(node, anchor, viewport, items->size());
  backend.FillRoundedRect(popup, render::CornerRadius::Uniform(5.0f), palette.surface);
  backend.StrokeRoundedRect(popup, render::CornerRadius::Uniform(5.0f), palette.border, 1.0f);
  const std::wstring current = Value(node, state);
  const std::size_t visible = static_cast<std::size_t>(popup.height / kSuggestionRowHeight);
  for (std::size_t index = 0; index < std::min(items->size(), visible); ++index) {
    const render::Rect row{popup.x + 2.0f,
                           popup.y + 2.0f + kSuggestionRowHeight * index,
                           popup.width - 4.0f, kSuggestionRowHeight - 2.0f};
    if ((*items)[index] == current) backend.FillRect(row, palette.control_pressed);
    backend.DrawTextRun((*items)[index], {row.x + 7.0f, row.y, row.width - 14.0f, row.height},
                        {}, palette.text, render::TextAlign::Left,
                        render::VerticalAlign::Middle);
  }
}

ComponentResult OverlayPointer(const config::ComponentNode& node,
                               const application::UiState& state, render::Point point,
                               const render::Rect& anchor, render::Size viewport) {
  const std::vector<std::wstring>* items = Suggestions(node, state);
  if (items == nullptr || items->empty()) return {};
  const render::Rect popup = PopupBounds(node, anchor, viewport, items->size());
  if (!popup.contains(point)) return {};
  const std::size_t index = static_cast<std::size_t>((point.y - popup.y) /
                                                     kSuggestionRowHeight);
  if (index >= items->size()) return {};
  const std::wstring& selected = (*items)[index];
  return Changed(node, selected, {selected.size(), selected.size()});
}

ComponentResult ContextMenu(const config::ComponentNode& node,
                            const application::UiState& state, void* owner_window) {
  const std::wstring value = Value(node, state);
  const Selection selection = ReadSelection(node, state, value.size());
  const bool editable = !node.GetBool(L"read_only");
  const bool copyable = !selection.empty() && !node.GetBool(L"password");
  HMENU menu = CreatePopupMenu();
  if (menu == nullptr) return {};
  AppendMenuW(menu, MF_STRING | (editable && copyable ? MF_ENABLED : MF_GRAYED),
              kMenuCut, L"Cut");
  AppendMenuW(menu, MF_STRING | (copyable ? MF_ENABLED : MF_GRAYED), kMenuCopy, L"Copy");
  AppendMenuW(menu, MF_STRING | (editable && ClipboardHasText() ? MF_ENABLED : MF_GRAYED),
              kMenuPaste, L"Paste");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING | (!value.empty() ? MF_ENABLED : MF_GRAYED),
              kMenuSelectAll, L"Select all");
  POINT point{};
  GetCursorPos(&point);
  const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                      point.x, point.y, 0,
                                      static_cast<HWND>(owner_window), nullptr);
  DestroyMenu(menu);
  if (command == kMenuCut) return Copy(node, state, true);
  if (command == kMenuCopy) return Copy(node, state, false);
  if (command == kMenuPaste) return Paste(node, state);
  if (command == kMenuSelectAll) return Select(node, {value.size(), 0});
  ComponentResult handled;
  handled.handled = true;
  return handled;
}
}  // namespace

ComponentDescriptor CreateInputComponent() {
  ComponentDescriptor descriptor{L"input", false, &Measure, &Paint, &TabFocusable, nullptr};
  descriptor.key = &Key;
  descriptor.text_input = &TextInput;
  descriptor.pointer = &Pointer;
  descriptor.paint_overlay = &PaintOverlay;
  descriptor.overlay_pointer = &OverlayPointer;
  descriptor.double_click = &DoubleClick;
  descriptor.context_menu = &ContextMenu;
  descriptor.has_overlay = &HasOverlay;
  return descriptor;
}
}  // namespace ui::components
