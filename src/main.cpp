// PrayerApp native: one .exe living in the tray. Starts the WebSocket print server on 8080,
// opens with Windows, updates itself from GitHub Releases.
#include "app.h"
#include "htmlprint.h"
#include "logger.h"
#include "resource.h"
#include "updater.h"
#include "util.h"
#include "wsserver.h"
#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlguid.h>
#include <cstdlib>
#include <exception>
#include <string>

HINSTANCE g_inst;
HWND g_mainWnd;

namespace {

enum {
  CMD_VERSION = 1, CMD_START, CMD_STOP, CMD_LOGS, CMD_TESTER, CMD_LOGFOLDER, CMD_AUTOSTART, CMD_ABOUT, CMD_QUIT
};
const UINT TIMER_FIRST_UPDATE = 1;
const wchar_t* RUN_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* RUN_VALUE = L"PrayerApp";

UINT g_taskbarCreated;
NOTIFYICONDATAW g_nid{};
HICON g_icon;
bool g_serverOn = false;
bool g_noInstall = false;
struct { bool checking = false, ready = false, manual = false; std::wstring version; } g_upd;

struct NoticeMsg { Notice kind; std::wstring title, text; };

void copyTrunc(wchar_t* dst, size_t cap, const std::wstring& s) {
  wcsncpy_s(dst, cap, s.c_str(), _TRUNCATE);
}

void updateTip() {
  std::wstring tip = std::wstring(L"PrayerApp v") + APP_VERSION_W + L"\nServidor: " + (g_serverOn ? L"online (porta 8080)" : L"offline") + L"\nDuplo clique: teste de impressão";
  copyTrunc(g_nid.szTip, ARRAYSIZE(g_nid.szTip), tip);
  g_nid.uFlags = NIF_TIP | NIF_SHOWTIP;
  Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

void addTray() {
  g_nid = {sizeof g_nid};
  g_nid.hWnd = g_mainWnd;
  g_nid.uID = 1;
  g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
  g_nid.uCallbackMessage = WM_APP_TRAY;
  g_nid.hIcon = g_icon;
  copyTrunc(g_nid.szTip, ARRAYSIZE(g_nid.szTip), L"PrayerApp");
  Shell_NotifyIconW(NIM_ADD, &g_nid);
  g_nid.uVersion = NOTIFYICON_VERSION_4;
  Shell_NotifyIconW(NIM_SETVERSION, &g_nid);
  updateTip();
}

void showBalloon(Notice kind, const std::wstring& title, const std::wstring& text) {
  NOTIFYICONDATAW n = g_nid;
  n.uFlags = NIF_INFO;
  copyTrunc(n.szInfoTitle, ARRAYSIZE(n.szInfoTitle), title);
  copyTrunc(n.szInfo, ARRAYSIZE(n.szInfo), text);
  n.dwInfoFlags = kind == Notice::Error ? NIIF_ERROR : kind == Notice::Warn ? NIIF_WARNING : NIIF_INFO;
  Shell_NotifyIconW(NIM_MODIFY, &n);
}

// --- autostart / install ---------------------------------------------------------------

std::wstring installDir() {
  wchar_t local[MAX_PATH];
  GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
  return std::wstring(local) + L"\\PrayerApp";
}

bool autostartEnabled() {
  wchar_t buf[1024];
  DWORD size = sizeof buf;
  return RegGetValueW(HKEY_CURRENT_USER, RUN_KEY, RUN_VALUE, RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS;
}

void setAutostart(bool on) {
  HKEY k;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) return;
  if (on) {
    std::wstring cmd = L"\"" + exePath() + L"\" --autostart";
    RegSetValueExW(k, RUN_VALUE, 0, REG_SZ, (const BYTE*)cmd.c_str(), (DWORD)((cmd.size() + 1) * sizeof(wchar_t)));
  } else {
    RegDeleteValueW(k, RUN_VALUE);
  }
  RegCloseKey(k);
}

void createStartMenuShortcut(const std::wstring& target) {
  PWSTR programs = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_Programs, 0, nullptr, &programs))) return;
  std::wstring lnk = std::wstring(programs) + L"\\PrayerApp.lnk";
  CoTaskMemFree(programs);
  IShellLinkW* link = nullptr;
  if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) {
    link->SetPath(target.c_str());
    link->SetDescription(L"PrayerApp - Gerenciador de Impressão");
    IPersistFile* file = nullptr;
    if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&file)))) {
      file->Save(lnk.c_str(), TRUE);
      file->Release();
    }
    link->Release();
  }
}

// Running from anywhere else (Downloads, a pen drive): copy itself to %LOCALAPPDATA%\PrayerApp
// and start from there, so it can open with Windows and update itself without admin rights.
// Returns true when this process must exit.
bool selfInstall() {
  std::wstring dir = installDir();
  std::wstring target = dir + L"\\PrayerApp.exe";
  std::wstring self = exePath();
  if (lowerW(self) == lowerW(target)) return false;
  CreateDirectoryW(dir.c_str(), nullptr);
  if (!CopyFileW(self.c_str(), target.c_str(), FALSE)) {
    DWORD err = GetLastError();
    // In use: an installed copy is running or locked. Use it instead of a second copy.
    if (GetFileAttributesW(target.c_str()) == INVALID_FILE_ATTRIBUTES) {
      std::wstring msg = L"Não foi possível instalar em " + dir + L":\n" + winErrorText(err) + L"\n\nO PrayerApp vai rodar daqui mesmo.";
      MessageBoxW(nullptr, msg.c_str(), L"PrayerApp", MB_ICONWARNING);
      return false;
    }
  }
  createStartMenuShortcut(target);
  // --after-update makes the copy wait for this process to exit (it holds the single-instance mutex).
  std::wstring cmd = L"\"" + target + L"\" --after-update " + std::to_wstring(GetCurrentProcessId());
  STARTUPINFOW si{sizeof si};
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(target.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return false;
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return true;
}

// --- actions -------------------------------------------------------------------------------

void setServer(bool on) {
  if (on) {
    g_serverOn = wsStart();
  } else {
    wsStop();
    g_serverOn = false;
    erro("Servidor WebSocket parado.");
  }
  updateTip();
}

void showAbout() {
  std::wstring text = std::wstring(L"Prayer - Gerenciador de Impressão\n\nVersão: ") + APP_VERSION_W +
                      L" (nativo)\nDesenvolvido por: JZ-TECH-SYS\n\nServidor WebSocket: porta 8080\nLogs: " + logDir();
  MSGBOXPARAMSW p{sizeof p};
  p.hwndOwner = nullptr;
  p.hInstance = g_inst;
  p.lpszText = text.c_str();
  p.lpszCaption = L"Sobre o Prayer";
  p.dwStyle = MB_USERICON;
  p.lpszIcon = MAKEINTRESOURCEW(IDI_APP);
  MessageBoxIndirectW(&p);
}

void installUpdate() {
  int r = MessageBoxW(nullptr, (L"A versão " + g_upd.version + L" já foi baixada. Deseja instalar agora?\n\nO aplicativo reiniciará para concluir a instalação.").c_str(),
                      L"Atualização pronta", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON1 | MB_SETFOREGROUND);
  if (r != IDYES) {
    logInfo("Atualização adiada", Json::object().set("versao", g_upd.version));
    return;
  }
  logInfo("Instalando atualização", Json::object().set("versao", g_upd.version));
  wsStop();
  std::wstring error;
  if (updaterInstall(error)) {
    DestroyWindow(g_mainWnd);
    return;
  }
  logError("Falha ao instalar atualização", Json::object().set("erro", error));
  showBalloon(Notice::Error, L"Falha na atualização", error);
  g_serverOn = wsStart();
  updateTip();
}

void showMenu() {
  HMENU m = CreatePopupMenu();
  std::wstring version = g_upd.ready ? L"Instalar atualização " + g_upd.version
                         : g_upd.checking ? L"Buscando atualizações..."
                                          : std::wstring(L"Versão atual v") + APP_VERSION_W + L" (buscar atualização)";
  AppendMenuW(m, MF_STRING | (g_upd.checking && !g_upd.ready ? MF_GRAYED : 0), CMD_VERSION, version.c_str());
  AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(m, MF_STRING | MF_GRAYED, 0, g_serverOn ? L"● Servidor online (porta 8080)" : L"○ Servidor offline");
  AppendMenuW(m, MF_STRING | (g_serverOn ? MF_GRAYED : 0), CMD_START, L"Iniciar servidor");
  AppendMenuW(m, MF_STRING | (g_serverOn ? 0 : MF_GRAYED), CMD_STOP, L"Parar servidor");
  AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(m, MF_STRING, CMD_LOGS, L"Ver logs");
  AppendMenuW(m, MF_STRING, CMD_TESTER, L"Teste de impressão");
  AppendMenuW(m, MF_STRING, CMD_LOGFOLDER, L"Pasta de logs");
  AppendMenuW(m, MF_STRING | (autostartEnabled() ? MF_CHECKED : 0), CMD_AUTOSTART, L"Abrir junto com o Windows");
  AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(m, MF_STRING, CMD_ABOUT, L"Sobre o Prayer");
  AppendMenuW(m, MF_STRING, CMD_QUIT, L"Sair");
  SetMenuDefaultItem(m, CMD_TESTER, FALSE);

  POINT pt;
  GetCursorPos(&pt);
  SetForegroundWindow(g_mainWnd);
  int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, g_mainWnd, nullptr);
  PostMessageW(g_mainWnd, WM_NULL, 0, 0);
  DestroyMenu(m);

  switch (cmd) {
    case CMD_VERSION:
      if (g_upd.ready) installUpdate();
      else {
        g_upd.manual = true;
        updaterCheck(true);
      }
      break;
    case CMD_START: setServer(true); break;
    case CMD_STOP: setServer(false); break;
    case CMD_LOGS: logInfo("Abrindo visualizador de logs via tray"); openLogViewer(); break;
    case CMD_TESTER: logInfo("Abrindo teste de impressão via tray"); openTester(); break;
    case CMD_LOGFOLDER: ShellExecuteW(nullptr, L"open", logDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL); break;
    case CMD_AUTOSTART: setAutostart(!autostartEnabled()); break;
    case CMD_ABOUT: showAbout(); break;
    case CMD_QUIT:
      logInfo("Encerrando aplicação via menu do tray");
      DestroyWindow(g_mainWnd);
      break;
  }
}

void onUpdateEvent(UpdateEvent ev, const std::wstring& detail) {
  bool manual = g_upd.manual;
  switch (ev) {
    case UPD_CHECKING:
      g_upd.checking = true;
      if (manual) showBalloon(Notice::Info, L"Buscando atualização", L"Verificando versão mais recente no GitHub");
      break;
    case UPD_AVAILABLE:
      logInfo("Atualização disponível", Json::object().set("versao", detail));
      showBalloon(Notice::Info, L"Atualização encontrada", L"Versão " + detail + L" será baixada em segundo plano");
      break;
    case UPD_NONE:
      g_upd.checking = g_upd.manual = false;
      logInfo("Nenhuma atualização disponível", Json::object().set("ultimaRelease", detail));
      if (manual) showBalloon(Notice::Ok, L"Você está atualizado", std::wstring(L"Versão atual: ") + APP_VERSION_W);
      break;
    case UPD_ERROR:
      g_upd.checking = g_upd.manual = false;
      logError("Erro no auto-update", Json::object().set("erro", detail));
      if (manual) showBalloon(Notice::Error, L"Falha no update", detail);
      break;
    case UPD_DOWNLOADED:
      g_upd.checking = g_upd.manual = false;
      g_upd.ready = true;
      g_upd.version = detail;
      logInfo("Atualização baixada", Json::object().set("versao", detail));
      showBalloon(Notice::Ok, L"Atualização pronta", L"Clique no ícone e escolha \"Instalar atualização\"");
      installUpdate();
      break;
  }
}

LRESULT CALLBACK mainProc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == g_taskbarCreated && g_taskbarCreated) {      // Explorer restarted: icon is gone
    addTray();
    return 0;
  }
  switch (msg) {
    case WM_APP_TRAY:
      switch (LOWORD(lp)) {
        case WM_LBUTTONDBLCLK: openTester(); break;
        case WM_CONTEXTMENU:
        case NIN_SELECT:
        case NIN_KEYSELECT: showMenu(); break;
        case NIN_BALLOONUSERCLICK: if (g_upd.ready) installUpdate(); break;
      }
      return 0;
    case WM_APP_NOTIFY: {
      auto* n = (NoticeMsg*)lp;
      showBalloon(n->kind, n->title, n->text);
      delete n;
      return 0;
    }
    case WM_APP_HTMLPRINT: htmlPrintPump(); return 0;
    case WM_APP_UPDATE: {
      auto* d = (std::wstring*)lp;
      onUpdateEvent((UpdateEvent)wp, d ? *d : L"");
      delete d;
      return 0;
    }
    case WM_TIMER:
      if (wp == TIMER_FIRST_UPDATE) {
        KillTimer(w, TIMER_FIRST_UPDATE);
        updaterCheck(false);
      } else if (wp == TIMER_HTMLPRINT) {
        htmlPrintTimeout();
      }
      return 0;
    case WM_QUERYENDSESSION: return TRUE;
    case WM_ENDSESSION:
      if (wp) {
        wsStop();
        logInfo("Prayer System encerrado (Windows desligando)");
      }
      return 0;
    case WM_DESTROY:
      wsStop();
      htmlPrintShutdown();
      Shell_NotifyIconW(NIM_DELETE, &g_nid);
      logInfo("Prayer System encerrado");
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(w, msg, wp, lp);
}

}  // namespace

// --- shared helpers (app.h) -----------------------------------------------------------------

void notify(Notice kind, const std::wstring& title, const std::wstring& text) {
  if (!g_mainWnd) return;
  auto* n = new NoticeMsg{kind, title, text};
  if (!PostMessageW(g_mainWnd, WM_APP_NOTIFY, 0, (LPARAM)n)) delete n;
}

void sucesso(const std::string& t) {
  notify(Notice::Ok, L"Sucesso", widen(t));
  logInfo("Notificação Sucesso", Json::object().set("mensagem", t));
}
void erro(const std::string& t) {
  notify(Notice::Error, L"Erro", widen(t));
  logError("Notificação Erro", Json::object().set("mensagem", t));
}
void alerta(const std::string& t) {
  notify(Notice::Warn, L"Alerta", widen(t));
  logWarn("Notificação Alerta", Json::object().set("mensagem", t));
}

HFONT uiFont() {
  static HFONT f = [] {
    NONCLIENTMETRICSW m{sizeof m};
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof m, &m, 0);
    return CreateFontIndirectW(&m.lfMessageFont);
  }();
  return f;
}

int dpi(HWND w, int px) { return MulDiv(px, (int)GetDpiForWindow(w), 96); }

HWND makeControl(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int id, DWORD exStyle) {
  HWND c = CreateWindowExW(exStyle, cls, text, WS_CHILD | WS_VISIBLE | WS_TABSTOP * (wcscmp(cls, L"STATIC") != 0) | style, 0, 0, 10, 10, parent,
                           (HMENU)(INT_PTR)id, g_inst, nullptr);
  SendMessageW(c, WM_SETFONT, (WPARAM)uiFont(), TRUE);
  return c;
}

// --- crash report: one line in the log with the code and where it happened -----------------

static LONG WINAPI crashFilter(EXCEPTION_POINTERS* ep) {
  auto* rec = ep->ExceptionRecord;
  HMODULE mod = nullptr;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)rec->ExceptionAddress, &mod);
  wchar_t modName[MAX_PATH] = L"?";
  if (mod) GetModuleFileNameW(mod, modName, MAX_PATH);
  char buf[160];
  snprintf(buf, sizeof buf, "0x%08lX em +0x%llX", rec->ExceptionCode, (unsigned long long)((ULONG_PTR)rec->ExceptionAddress - (ULONG_PTR)mod));
  logError("PrayerApp travou (exceção não tratada)", Json::object().set("excecao", buf).set("modulo", std::wstring(modName)).set("thread", GetCurrentThreadId()));
  return EXCEPTION_CONTINUE_SEARCH;
}

// --- entry ---------------------------------------------------------------------------------

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
  g_inst = inst;
  SetUnhandledExceptionFilter(crashFilter);
  std::set_terminate([] {
    std::string what = "sem detalhe";
    try {
      if (auto e = std::current_exception()) std::rethrow_exception(e);
    } catch (const std::exception& ex) {
      what = ex.what();
    } catch (...) {
      what = "exceção não-std";
    }
    logError("PrayerApp travou (std::terminate)", Json::object().set("motivo", what).set("thread", GetCurrentThreadId()));
    abort();
  });
  _set_invalid_parameter_handler([](const wchar_t* expr, const wchar_t* fn, const wchar_t* file, unsigned line, uintptr_t) {
    logError("PrayerApp travou (parâmetro inválido no CRT)", Json::object().set("thread", GetCurrentThreadId()));
    TerminateProcess(GetCurrentProcess(), 3);
  });
  int argc = 0;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  bool installed = false;
  for (int i = 1; i < argc; i++) {
    std::wstring a = argv[i];
    if (a == L"--no-install") g_noInstall = true;
    else if (a == L"--installed") installed = true;
    else if (a == L"--after-update" && i + 1 < argc) {
      // The previous version is closing: wait for it to free the mutex and the port.
      HANDLE prev = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)_wtoi(argv[++i]));
      if (prev) {
        WaitForSingleObject(prev, 20000);
        CloseHandle(prev);
      }
      installed = true;
    }
  }
  LocalFree(argv);

  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  SetCurrentProcessExplicitAppUserModelID(L"com.prayer.app");

  HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\PrayerApp.SingleInstance");
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    // Already running: just say so, the tray icon is there.
    MessageBoxW(nullptr, L"O PrayerApp já está aberto. Procure o ícone da impressora perto do relógio.", L"PrayerApp", MB_ICONINFORMATION | MB_SETFOREGROUND);
    return 0;
  }
  if (!g_noInstall && !installed && selfInstall()) {
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 0;
  }
  if (!g_noInstall) {
    updaterCleanup();
    setAutostart(true);                                    // open with Windows (only the installed copy)
  }

  INITCOMMONCONTROLSEX icc{sizeof icc, ICC_STANDARD_CLASSES | ICC_TAB_CLASSES};
  InitCommonControlsEx(&icc);
  g_icon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);

  WNDCLASSEXW wc{sizeof wc};
  wc.lpfnWndProc = mainProc;
  wc.hInstance = inst;
  wc.lpszClassName = L"PrayerAppMain";
  RegisterClassExW(&wc);
  // Hidden top-level window (not message-only: it must receive TaskbarCreated broadcasts).
  g_mainWnd = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"PrayerApp", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, inst, nullptr);
  g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
  ChangeWindowMessageFilterEx(g_mainWnd, g_taskbarCreated, MSGFLT_ALLOW, nullptr);

  addTray();
  logInfo("Prayer System iniciado (nativo)", Json::object().set("versao", APP_VERSION).set("exe", exePath()));
  g_serverOn = wsStart();
  updateTip();
  if (!g_noInstall) SetTimer(g_mainWnd, TIMER_FIRST_UPDATE, 4000, nullptr);

  // GetMessage can return -1 (seen right after WebView2 starts navigating, error 1400); that is
  // not WM_QUIT, so the loop keeps going instead of closing the app silently.
  MSG msg;
  int failures = 0;
  for (;;) {
    BOOL r = GetMessageW(&msg, nullptr, 0, 0);
    if (r == 0) break;
    if (r == -1) {
      if (++failures == 1 || failures % 1000 == 0) logWarn("GetMessage retornou -1", Json::object().set("erro", winErrorText(GetLastError())).set("vezes", failures));
      Sleep(1);
      continue;
    }
    failures = 0;
    if (testerPreTranslate(&msg) || logViewerPreTranslate(&msg)) continue;
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  ReleaseMutex(mutex);
  CloseHandle(mutex);
  CoUninitialize();
  return 0;
}
