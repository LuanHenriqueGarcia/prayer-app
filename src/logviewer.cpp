// Log viewer: newest line on top, file picker (one file per day), search filter, live tail of today's file.
#include "app.h"
#include "logger.h"
#include "util.h"
#include <commctrl.h>
#include <shellapi.h>
#include <algorithm>
#include <deque>
#include <vector>

namespace {

enum { ID_OPEN = 100, ID_FILES, ID_CLEAR, ID_SEARCH, ID_CLEARSEARCH, ID_CONTENT };
const size_t MAX_LINES = 5000;

HWND g_wnd, g_open, g_files, g_clear, g_path, g_search, g_clearSearch, g_count, g_content;
HFONT g_mono;
std::deque<std::wstring> g_lines;                 // oldest first
std::vector<std::wstring> g_fileList;
std::wstring g_current;

std::wstring readTail(const std::wstring& path) {
  HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
  if (f == INVALID_HANDLE_VALUE) return L"";
  LARGE_INTEGER size{};
  GetFileSizeEx(f, &size);
  const LONGLONG maxBytes = 4 * 1024 * 1024;
  LARGE_INTEGER pos{};
  pos.QuadPart = std::max<LONGLONG>(0, size.QuadPart - maxBytes);
  SetFilePointerEx(f, pos, nullptr, FILE_BEGIN);
  std::string data((size_t)(size.QuadPart - pos.QuadPart), '\0');
  DWORD got = 0;
  ReadFile(f, data.data(), (DWORD)data.size(), &got, nullptr);
  CloseHandle(f);
  data.resize(got);
  return widen(data);
}

void addLines(const std::wstring& text) {
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find(L'\n', start);
    if (end == std::wstring::npos) end = text.size();
    std::wstring line = text.substr(start, end - start);
    if (!line.empty() && line.back() == L'\r') line.pop_back();
    if (!line.empty()) g_lines.push_back(std::move(line));
    start = end + 1;
  }
  while (g_lines.size() > MAX_LINES) g_lines.pop_front();
}

std::wstring query() {
  wchar_t buf[512] = L"";
  GetWindowTextW(g_search, buf, 512);
  return lowerW(buf);
}

bool matches(const std::wstring& line, const std::wstring& q) { return q.empty() || lowerW(line).find(q) != std::wstring::npos; }

void render() {
  std::wstring q = query();
  std::wstring text;
  size_t matched = 0;
  for (auto it = g_lines.rbegin(); it != g_lines.rend(); ++it) {
    if (!matches(*it, q)) continue;
    matched++;
    text += *it;
    text += L"\r\n";
  }
  SetWindowTextW(g_content, text.c_str());
  std::wstring count = q.empty() ? L"" : std::to_wstring(matched) + L" de " + std::to_wstring(g_lines.size());
  SetWindowTextW(g_count, count.c_str());
}

void loadFile(const std::wstring& path) {
  g_current = path;
  g_lines.clear();
  addLines(readTail(path));
  SetWindowTextW(g_path, path.c_str());
  render();
}

void fillFileList() {
  g_fileList.clear();
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileW((logDir() + L"\\prayer-*.log").c_str(), &fd);
  if (h != INVALID_HANDLE_VALUE) {
    do g_fileList.push_back(fd.cFileName);
    while (FindNextFileW(h, &fd));
    FindClose(h);
  }
  std::sort(g_fileList.begin(), g_fileList.end(), std::greater<>());
  SendMessageW(g_files, CB_RESETCONTENT, 0, 0);
  std::wstring today = logFilePath();
  today = today.substr(today.find_last_of(L'\\') + 1);
  int sel = 0;
  for (size_t i = 0; i < g_fileList.size(); i++) {
    SendMessageW(g_files, CB_ADDSTRING, 0, (LPARAM)g_fileList[i].c_str());
    if (g_fileList[i] == today) sel = (int)i;
  }
  SendMessageW(g_files, CB_SETCURSEL, sel, 0);
}

void layout() {
  RECT r;
  GetClientRect(g_wnd, &r);
  int m = dpi(g_wnd, 8), h = dpi(g_wnd, 26), x = m;
  auto place = [&](HWND c, int w) { MoveWindow(c, x, m, w, h, TRUE); x += w + m; };
  place(g_open, dpi(g_wnd, 120));
  MoveWindow(g_files, x, m, dpi(g_wnd, 200), dpi(g_wnd, 300), TRUE);
  x += dpi(g_wnd, 200) + m;
  place(g_clear, dpi(g_wnd, 70));
  int right = r.right - m;
  int countW = dpi(g_wnd, 80), xW = dpi(g_wnd, 28), searchW = dpi(g_wnd, 220);
  MoveWindow(g_count, right - countW, m + dpi(g_wnd, 5), countW, h, TRUE);
  MoveWindow(g_clearSearch, right - countW - m - xW, m, xW, h, TRUE);
  MoveWindow(g_search, right - countW - 2 * m - xW - searchW, m, searchW, h, TRUE);
  MoveWindow(g_path, m, 2 * m + h, r.right - 2 * m, dpi(g_wnd, 18), TRUE);
  int top = 2 * m + h + dpi(g_wnd, 22);
  MoveWindow(g_content, m, top, r.right - 2 * m, r.bottom - top - m, TRUE);
}

LRESULT CALLBACK proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_CREATE: {
      g_wnd = w;
      g_open = makeControl(w, L"BUTTON", L"Abrir arquivo atual", BS_PUSHBUTTON, ID_OPEN);
      g_files = makeControl(w, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL, ID_FILES);
      g_clear = makeControl(w, L"BUTTON", L"Limpar", BS_PUSHBUTTON, ID_CLEAR);
      g_path = makeControl(w, L"STATIC", L"", SS_PATHELLIPSIS, 0);
      g_search = makeControl(w, L"EDIT", L"", ES_AUTOHSCROLL, ID_SEARCH, WS_EX_CLIENTEDGE);
      SendMessageW(g_search, EM_SETCUEBANNER, TRUE, (LPARAM)L"Buscar nos logs...");
      g_clearSearch = makeControl(w, L"BUTTON", L"×", BS_PUSHBUTTON, ID_CLEARSEARCH);
      g_count = makeControl(w, L"STATIC", L"", 0, 0);
      g_content = makeControl(w, L"EDIT", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL | WS_VSCROLL | WS_HSCROLL, ID_CONTENT, WS_EX_CLIENTEDGE);
      SendMessageW(g_content, EM_SETLIMITTEXT, 0, 0);
      g_mono = CreateFontW(-dpi(w, 13), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, FIXED_PITCH, L"Consolas");
      SendMessageW(g_content, WM_SETFONT, (WPARAM)g_mono, TRUE);
      fillFileList();
      loadFile(logFilePath());
      logSetListener(w, WM_APP_LOGLINE);
      layout();
      return 0;
    }
    case WM_SIZE: layout(); return 0;
    case WM_GETMINMAXINFO: ((MINMAXINFO*)lp)->ptMinTrackSize = {dpi(w, 760), dpi(w, 300)}; return 0;
    case WM_APP_LOGLINE: {
      std::wstring* line = (std::wstring*)lp;
      if (lowerW(g_current) == lowerW(logFilePath())) {
        size_t before = g_lines.size();
        addLines(*line);
        std::wstring q = query(), add;
        for (size_t i = g_lines.size(); i-- > std::min(before, g_lines.size());) {
          if (matches(g_lines[i], q)) add += g_lines[i] + L"\r\n";
        }
        if (g_lines.size() >= MAX_LINES) render();     // trimmed: redraw everything
        else if (!add.empty()) {
          SendMessageW(g_content, EM_SETSEL, 0, 0);
          SendMessageW(g_content, EM_REPLACESEL, FALSE, (LPARAM)add.c_str());
          SendMessageW(g_content, EM_SETSEL, 0, 0);
          SendMessageW(g_content, EM_SCROLLCARET, 0, 0);
          if (!q.empty()) render();
        }
      }
      delete line;
      return 0;
    }
    case WM_COMMAND:
      switch (LOWORD(wp)) {
        case ID_OPEN: ShellExecuteW(w, L"open", logFilePath().c_str(), nullptr, nullptr, SW_SHOWNORMAL); break;
        case ID_CLEAR: g_lines.clear(); render(); break;
        case ID_CLEARSEARCH: SetWindowTextW(g_search, L""); SetFocus(g_search); break;
        case ID_SEARCH: if (HIWORD(wp) == EN_CHANGE) render(); break;
        case ID_FILES:
          if (HIWORD(wp) == CBN_SELCHANGE) {
            int i = (int)SendMessageW(g_files, CB_GETCURSEL, 0, 0);
            if (i >= 0 && i < (int)g_fileList.size()) loadFile(logDir() + L"\\" + g_fileList[i]);
          } else if (HIWORD(wp) == CBN_DROPDOWN) {
            std::wstring cur = g_current.substr(g_current.find_last_of(L'\\') + 1);
            fillFileList();
            for (size_t i = 0; i < g_fileList.size(); i++)
              if (g_fileList[i] == cur) SendMessageW(g_files, CB_SETCURSEL, i, 0);
          }
          break;
      }
      return 0;
    case WM_DESTROY:
      logSetListener(nullptr, 0);
      if (g_mono) DeleteObject(g_mono);
      g_mono = nullptr;
      g_wnd = nullptr;
      g_lines.clear();
      return 0;
  }
  return DefWindowProcW(w, msg, wp, lp);
}

}  // namespace

void openLogViewer() {
  if (g_wnd) {
    ShowWindow(g_wnd, SW_RESTORE);
    SetForegroundWindow(g_wnd);
    return;
  }
  static bool registered = false;
  if (!registered) {
    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = proc;
    wc.hInstance = g_inst;
    wc.hIcon = LoadIconW(g_inst, MAKEINTRESOURCEW(101));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"PrayerLogViewer";
    RegisterClassExW(&wc);
    registered = true;
  }
  HWND w = CreateWindowExW(0, L"PrayerLogViewer", L"Logs - Prayer", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1000, 640, nullptr, nullptr, g_inst, nullptr);
  ShowWindow(w, SW_SHOWNORMAL);
  SetForegroundWindow(w);
}

bool logViewerPreTranslate(MSG* msg) { return g_wnd && IsDialogMessageW(g_wnd, msg); }
