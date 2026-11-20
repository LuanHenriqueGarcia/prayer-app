// Printer tester: pick a printer, print RAW text or HTML, see the result. Ctrl+P prints, Ctrl+R reloads.
#include "app.h"
#include "actions.h"
#include "logger.h"
#include "printers.h"
#include "util.h"
#include <commctrl.h>
#include <objbase.h>
#include <thread>
#include <vector>

namespace {

enum { ID_PRINTER = 200, ID_REFRESH, ID_TABS, ID_TEXT, ID_HTML, ID_PRINT, ID_RESULT };

HWND g_wnd, g_lblPrinter, g_printer, g_refresh, g_meta, g_tabs, g_text, g_html, g_print, g_result;
HFONT g_mono;
std::vector<PrinterInfo> g_list;
bool g_busy = false;

const wchar_t* DEFAULT_TEXT =
    L"Teste de impressão RAW - Prayer System\r\nData: {data}\r\nStatus: Teste em andamento\r\n----------------------------------------\r\n"
    L"Item 1: Produto A\r\nItem 2: Produto B\r\nItem 3: Produto C\r\n----------------------------------------\r\nTotal: R$ 123,45\r\n";

const wchar_t* DEFAULT_HTML =
    L"<div style=\"padding:16px;font-family:'Segoe UI',Arial,sans-serif;font-size:14px;max-width:600px\">\r\n"
    L"  <div style=\"text-align:center;border-bottom:2px solid #333;padding-bottom:10px;margin-bottom:15px\">\r\n"
    L"    <h2 style=\"margin:0;color:#2c3e50\">Prayer System</h2>\r\n"
    L"    <p style=\"margin:5px 0;color:#7f8c8d;font-size:12px\">Teste de Impressão HTML</p>\r\n"
    L"  </div>\r\n"
    L"  <div style=\"margin-bottom:15px\"><strong>Data:</strong> {data}<br><strong>Status:</strong> <span style=\"color:#27ae60\">Ativo</span></div>\r\n"
    L"  <table style=\"width:100%;border-collapse:collapse;margin-bottom:15px\">\r\n"
    L"    <tr style=\"background:#f8f9fa\"><th style=\"border:1px solid #ddd;padding:8px;text-align:left\">Item</th><th style=\"border:1px solid #ddd;padding:8px;text-align:right\">Valor</th></tr>\r\n"
    L"    <tr><td style=\"border:1px solid #ddd;padding:8px\">Produto A</td><td style=\"border:1px solid #ddd;padding:8px;text-align:right\">R$ 45,00</td></tr>\r\n"
    L"    <tr><td style=\"border:1px solid #ddd;padding:8px\">Produto B</td><td style=\"border:1px solid #ddd;padding:8px;text-align:right\">R$ 78,45</td></tr>\r\n"
    L"    <tr style=\"background:#f8f9fa;font-weight:bold\"><td style=\"border:1px solid #ddd;padding:8px\">Total</td><td style=\"border:1px solid #ddd;padding:8px;text-align:right\">R$ 123,45</td></tr>\r\n"
    L"  </table>\r\n"
    L"  <div style=\"text-align:center;color:#7f8c8d;font-size:11px;margin-top:20px\">Gerado pelo Prayer System - {data}</div>\r\n"
    L"</div>\r\n";

std::wstring getText(HWND c) {
  int n = GetWindowTextLengthW(c);
  std::wstring s(n + 1, L'\0');
  GetWindowTextW(c, s.data(), n + 1);
  s.resize(n);
  return s;
}

void setResult(const std::wstring& s) { SetWindowTextW(g_result, s.c_str()); }

bool htmlTab() { return TabCtrl_GetCurSel(g_tabs) == 1; }

void showTab() {
  bool html = htmlTab();
  ShowWindow(g_text, html ? SW_HIDE : SW_SHOW);
  ShowWindow(g_html, html ? SW_SHOW : SW_HIDE);
  SetWindowTextW(g_print, html ? L"Imprimir HTML" : L"Imprimir texto RAW");
}

void updateMeta() {
  int i = (int)SendMessageW(g_printer, CB_GETCURSEL, 0, 0);
  if (i < 0 || i >= (int)g_list.size()) {
    SetWindowTextW(g_meta, L"Selecione uma impressora para ver detalhes");
    return;
  }
  auto& p = g_list[i];
  std::wstring s = L"Driver: " + (p.driverName.empty() ? L"N/A" : p.driverName) + L"    Porta: " + (p.portName.empty() ? L"N/A" : p.portName) +
                   L"    Status: " + (p.workOffline ? L"Offline" : L"Online") + (p.shared ? L"    Compartilhada: " + p.shareName : L"");
  SetWindowTextW(g_meta, s.c_str());
}

void loadPrinters() {
  std::wstring selected;
  int cur = (int)SendMessageW(g_printer, CB_GETCURSEL, 0, 0);
  if (cur >= 0 && cur < (int)g_list.size()) selected = g_list[cur].name;
  g_list = listPrinters();
  SendMessageW(g_printer, CB_RESETCONTENT, 0, 0);
  int sel = 0;
  for (size_t i = 0; i < g_list.size(); i++) {
    std::wstring label = g_list[i].name + (g_list[i].shareName.empty() ? L"" : L"  (Rede: " + g_list[i].shareName + L")");
    SendMessageW(g_printer, CB_ADDSTRING, 0, (LPARAM)label.c_str());
    if (g_list[i].name == selected) sel = (int)i;
  }
  SendMessageW(g_printer, CB_SETCURSEL, g_list.empty() ? -1 : sel, 0);
  updateMeta();
  setResult(L"Atualização de impressoras concluída\r\n\r\n" + std::to_wstring(g_list.size()) + L" impressora(s) encontrada(s) às " + localDateTime(L"dd/MM/yyyy", L"HH:mm:ss") +
            L"\r\n\r\nSelecione uma impressora acima para começar a imprimir.");
}

void replaceAll(std::wstring& s, const std::wstring& from, const std::wstring& to) {
  for (size_t p = 0; (p = s.find(from, p)) != std::wstring::npos; p += to.size()) s.replace(p, from.size(), to);
}

void print() {
  if (g_busy) return;
  int i = (int)SendMessageW(g_printer, CB_GETCURSEL, 0, 0);
  if (i < 0 || i >= (int)g_list.size()) {
    setResult(L"ERRO: nenhuma impressora selecionada.\r\nSelecione uma impressora na lista acima antes de imprimir.");
    return;
  }
  bool html = htmlTab();
  std::wstring content = getText(html ? g_html : g_text);
  if (content.find_first_not_of(L" \r\n\t") == std::wstring::npos) {
    setResult(L"ERRO: conteúdo vazio.");
    return;
  }
  replaceAll(content, L"{data}", localDateTime());
  if (!html) replaceAll(content, L"\r\n", L"\n");
  Json req = Json::object();
  req.set("acao", html ? "imprimirHTML" : "imprimir").set("impressora", g_list[i].name).set("msg", content);
  g_busy = true;
  EnableWindow(g_print, FALSE);
  setResult(html ? L"Renderizando HTML e enviando para a impressora..." : L"Enviando para a impressora...");
  logInfo(html ? "Teste de impressão HTML (tela)" : "Teste de impressão RAW (tela)", Json::object().set("impressora", g_list[i].name));
  HWND target = g_wnd;
  std::thread([req, target] {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Json res = handleAction(req);
    CoUninitialize();
    auto* out = new std::string(res.dump());
    if (!PostMessageW(target, WM_APP_TESTER_DONE, 0, (LPARAM)out)) delete out;
  }).detach();
}

void layout() {
  RECT r;
  GetClientRect(g_wnd, &r);
  int m = dpi(g_wnd, 10), h = dpi(g_wnd, 26), y = m, W = r.right - 2 * m;
  MoveWindow(g_lblPrinter, m, y, W, dpi(g_wnd, 18), TRUE);
  y += dpi(g_wnd, 20);
  int bw = dpi(g_wnd, 100);
  MoveWindow(g_printer, m, y, W - bw - m, dpi(g_wnd, 300), TRUE);
  MoveWindow(g_refresh, r.right - m - bw, y, bw, h, TRUE);
  y += h + dpi(g_wnd, 4);
  MoveWindow(g_meta, m, y, W, dpi(g_wnd, 18), TRUE);
  y += dpi(g_wnd, 26);
  int resultH = dpi(g_wnd, 110);
  int tabsH = r.bottom - y - m - resultH - m - h - m;
  MoveWindow(g_tabs, m, y, W, tabsH, TRUE);
  RECT tr{m, y, m + W, y + tabsH};
  TabCtrl_AdjustRect(g_tabs, FALSE, &tr);
  MoveWindow(g_text, tr.left + 2, tr.top + 2, tr.right - tr.left - 4, tr.bottom - tr.top - 4, TRUE);
  MoveWindow(g_html, tr.left + 2, tr.top + 2, tr.right - tr.left - 4, tr.bottom - tr.top - 4, TRUE);
  y += tabsH + m;
  MoveWindow(g_print, m, y, dpi(g_wnd, 160), h, TRUE);
  y += h + m;
  MoveWindow(g_result, m, y, W, r.bottom - y - m, TRUE);
}

LRESULT CALLBACK proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_CREATE: {
      g_wnd = w;
      g_busy = false;
      g_lblPrinter = makeControl(w, L"STATIC", L"Impressora destino", 0, 0);
      g_printer = makeControl(w, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL, ID_PRINTER);
      g_refresh = makeControl(w, L"BUTTON", L"Atualizar", BS_PUSHBUTTON, ID_REFRESH);
      g_meta = makeControl(w, L"STATIC", L"", SS_ENDELLIPSIS, 0);
      g_tabs = makeControl(w, WC_TABCONTROLW, L"", WS_CLIPSIBLINGS, ID_TABS);
      TCITEMW ti{TCIF_TEXT};
      ti.pszText = (LPWSTR)L"Texto RAW";
      TabCtrl_InsertItem(g_tabs, 0, &ti);
      ti.pszText = (LPWSTR)L"HTML";
      TabCtrl_InsertItem(g_tabs, 1, &ti);
      DWORD editStyle = ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL | ES_AUTOHSCROLL | WS_VSCROLL | WS_HSCROLL;
      g_text = makeControl(w, L"EDIT", DEFAULT_TEXT, editStyle, ID_TEXT, WS_EX_CLIENTEDGE);
      g_html = makeControl(w, L"EDIT", DEFAULT_HTML, editStyle, ID_HTML, WS_EX_CLIENTEDGE);
      g_print = makeControl(w, L"BUTTON", L"Imprimir texto RAW", BS_DEFPUSHBUTTON, ID_PRINT);
      g_result = makeControl(w, L"EDIT", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL, ID_RESULT, WS_EX_CLIENTEDGE);
      g_mono = CreateFontW(-dpi(w, 13), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, FIXED_PITCH, L"Consolas");
      for (HWND c : {g_text, g_html, g_result}) SendMessageW(c, WM_SETFONT, (WPARAM)g_mono, TRUE);
      SetWindowPos(g_text, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
      SetWindowPos(g_html, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
      showTab();
      layout();
      loadPrinters();
      return 0;
    }
    case WM_SIZE: layout(); return 0;
    case WM_GETMINMAXINFO: ((MINMAXINFO*)lp)->ptMinTrackSize = {dpi(w, 520), dpi(w, 520)}; return 0;
    case WM_NOTIFY:
      if (((NMHDR*)lp)->idFrom == ID_TABS && ((NMHDR*)lp)->code == TCN_SELCHANGE) showTab();
      return 0;
    case WM_COMMAND:
      switch (LOWORD(wp)) {
        case ID_REFRESH: loadPrinters(); break;
        case ID_PRINT: print(); break;
        case ID_PRINTER: if (HIWORD(wp) == CBN_SELCHANGE) updateMeta(); break;
      }
      return 0;
    case WM_APP_TESTER_DONE: {
      std::string* raw = (std::string*)lp;
      Json res;
      Json::parse(*raw, res);
      delete raw;
      g_busy = false;
      EnableWindow(g_print, TRUE);
      bool ok = res.str("status") == "success";
      int i = (int)SendMessageW(g_printer, CB_GETCURSEL, 0, 0);
      std::wstring name = i >= 0 && i < (int)g_list.size() ? g_list[i].name : L"";
      setResult(std::wstring(ok ? L"SUCESSO" : L"ERRO") + L" às " + localDateTime(L"dd/MM/yyyy", L"HH:mm:ss") + L"\r\nImpressora: " + name +
                L"\r\nAção: " + widen(res.str("acao")) + L"\r\nMensagem: " + widen(res.str("message")) +
                (ok ? L"" : L"\r\n\r\nDica: confira se a impressora está ligada e conectada. Ctrl+R atualiza a lista."));
      return 0;
    }
    case WM_DESTROY:
      if (g_mono) DeleteObject(g_mono);
      g_mono = nullptr;
      g_wnd = nullptr;
      return 0;
  }
  return DefWindowProcW(w, msg, wp, lp);
}

}  // namespace

void openTester() {
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
    wc.lpszClassName = L"PrayerTester";
    RegisterClassExW(&wc);
    registered = true;
  }
  HWND w = CreateWindowExW(0, L"PrayerTester", L"Teste de Impressão - Prayer", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 760, 700, nullptr, nullptr, g_inst, nullptr);
  ShowWindow(w, SW_SHOWNORMAL);
  SetForegroundWindow(w);
}

bool testerPreTranslate(MSG* msg) {
  if (!g_wnd) return false;
  if (msg->message == WM_KEYDOWN && (GetKeyState(VK_CONTROL) & 0x8000) && (msg->hwnd == g_wnd || IsChild(g_wnd, msg->hwnd))) {
    if (msg->wParam == 'P') { print(); return true; }
    if (msg->wParam == 'R') { loadPrinters(); return true; }
  }
  // Tab/Enter navigation, except inside the multi-line editors (they keep Enter and Tab for text).
  if (msg->hwnd == g_text || msg->hwnd == g_html) return false;
  return IsDialogMessageW(g_wnd, msg);
}
