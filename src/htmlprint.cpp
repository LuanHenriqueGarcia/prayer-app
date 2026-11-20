#include "htmlprint.h"
#include "app.h"
#include "logger.h"
#include "util.h"
#include <wrl.h>
#include <WebView2.h>
#include <deque>
#include <memory>
#include <mutex>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace {

struct Job {
  std::string html;
  std::wstring printer;
  std::wstring tempFile;
  bool ok = false;
  std::wstring error;
  HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  ~Job() { CloseHandle(done); if (!tempFile.empty()) DeleteFileW(tempFile.c_str()); }
};

std::mutex g_mutex;
std::deque<std::shared_ptr<Job>> g_queue;

// UI-thread state
enum class State { Idle, Creating, Ready, Failed } g_state = State::Idle;
HRESULT g_createError = S_OK;
HWND g_host = nullptr;
ComPtr<ICoreWebView2Environment> g_env;
ComPtr<ICoreWebView2Controller> g_controller;
ComPtr<ICoreWebView2> g_view;
std::shared_ptr<Job> g_current;
EventRegistrationToken g_navToken{};

void finish(bool ok, const std::wstring& error) {
  KillTimer(g_mainWnd, TIMER_HTMLPRINT);
  if (!g_current) return;
  g_current->ok = ok;
  g_current->error = error;
  SetEvent(g_current->done);
  g_current.reset();
  PostMessageW(g_mainWnd, WM_APP_HTMLPRINT, 0, 0);    // next job, if any
}

void failAll(const std::wstring& error) {
  std::lock_guard<std::mutex> lock(g_mutex);
  for (auto& j : g_queue) {
    j->error = error;
    SetEvent(j->done);
  }
  g_queue.clear();
}

void createWebView() {
  g_state = State::Creating;
  if (!g_host) {
    g_host = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"PrayerApp HTML print", WS_POPUP, -32000, -32000, 800, 1000,
                             nullptr, nullptr, g_inst, nullptr);
  }
  wchar_t local[MAX_PATH];
  GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
  std::wstring dataDir = std::wstring(local) + L"\\PrayerApp\\WebView2";

  HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, dataDir.c_str(), nullptr,
    Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
      [](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
        if (FAILED(hr) || !env) {
          g_state = State::Failed;
          g_createError = FAILED(hr) ? hr : E_FAIL;
          PostMessageW(g_mainWnd, WM_APP_HTMLPRINT, 0, 0);
          return S_OK;
        }
        g_env = env;
        return env->CreateCoreWebView2Controller(g_host,
          Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
            [](HRESULT hr, ICoreWebView2Controller* controller) -> HRESULT {
              if (FAILED(hr) || !controller) {
                g_state = State::Failed;
                g_createError = FAILED(hr) ? hr : E_FAIL;
              } else {
                g_controller = controller;
                g_controller->get_CoreWebView2(&g_view);
                RECT r{0, 0, 800, 1000};
                g_controller->put_Bounds(r);
                ComPtr<ICoreWebView2Settings> s;
                if (SUCCEEDED(g_view->get_Settings(&s))) {
                  s->put_AreDevToolsEnabled(FALSE);
                  s->put_AreDefaultContextMenusEnabled(FALSE);
                  s->put_IsStatusBarEnabled(FALSE);
                }
                g_state = State::Ready;
              }
              PostMessageW(g_mainWnd, WM_APP_HTMLPRINT, 0, 0);
              return S_OK;
            }).Get());
      }).Get());
  if (FAILED(hr)) {
    g_state = State::Failed;
    g_createError = hr;
  }
}

void startPrint() {
  ComPtr<ICoreWebView2_16> view16;
  ComPtr<ICoreWebView2Environment6> env6;
  ComPtr<ICoreWebView2PrintSettings> settings;
  ComPtr<ICoreWebView2PrintSettings2> settings2;
  if (FAILED(g_view.As(&view16)) || FAILED(g_env.As(&env6)) || FAILED(env6->CreatePrintSettings(&settings)) ||
      FAILED(settings.As(&settings2))) {
    finish(false, L"WebView2 Runtime muito antigo para impressão silenciosa. Atualize o Microsoft Edge WebView2.");
    return;
  }
  settings->put_MarginTop(0);
  settings->put_MarginBottom(0);
  settings->put_MarginLeft(0);
  settings->put_MarginRight(0);
  settings->put_ShouldPrintBackgrounds(TRUE);
  settings->put_ShouldPrintHeaderAndFooter(FALSE);
  settings2->put_PrinterName(g_current->printer.c_str());

  HRESULT hr = view16->Print(settings.Get(),
    Callback<ICoreWebView2PrintCompletedHandler>([](HRESULT hr, COREWEBVIEW2_PRINT_STATUS status) -> HRESULT {
      if (FAILED(hr)) finish(false, L"Falha ao imprimir: " + winErrorText((DWORD)hr));
      else if (status == COREWEBVIEW2_PRINT_STATUS_SUCCEEDED) finish(true, L"");
      else if (status == COREWEBVIEW2_PRINT_STATUS_PRINTER_UNAVAILABLE) finish(false, L"Impressora indisponível");
      else finish(false, L"Falha ao imprimir");
      return S_OK;
    }).Get());
  if (FAILED(hr)) finish(false, L"Falha ao imprimir: " + winErrorText((DWORD)hr));
}

void startJob() {
  if (!g_navToken.value) {
    g_view->add_NavigationCompleted(
      Callback<ICoreWebView2NavigationCompletedEventHandler>([](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
        if (!g_current) return S_OK;
        BOOL ok = FALSE;
        args->get_IsSuccess(&ok);
        if (ok) startPrint();
        else finish(false, L"Não foi possível carregar o HTML");
        return S_OK;
      }).Get(), &g_navToken);
  }
  SetTimer(g_mainWnd, TIMER_HTMLPRINT, 90000, nullptr);
  // NavigateToString is limited to 2 MB; larger documents go through a temp file.
  HRESULT hr;
  if (g_current->html.size() < 1500000) {
    hr = g_view->NavigateToString(widen(g_current->html).c_str());
  } else {
    wchar_t name[64];
    swprintf(name, 64, L"\\html-%lu-%lu.html", GetCurrentProcessId(), GetTickCount());
    g_current->tempFile = appTempDir() + name;
    HANDLE f = CreateFileW(g_current->tempFile.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    DWORD w = 0;
    if (f != INVALID_HANDLE_VALUE) {
      WriteFile(f, g_current->html.data(), (DWORD)g_current->html.size(), &w, nullptr);
      CloseHandle(f);
    }
    std::wstring url = L"file:///" + g_current->tempFile;
    for (auto& c : url) if (c == L'\\') c = L'/';
    hr = g_view->Navigate(url.c_str());
  }
  if (FAILED(hr)) finish(false, L"Não foi possível carregar o HTML: " + winErrorText((DWORD)hr));
}

}  // namespace

bool printHtml(const std::string& html, const std::wstring& printer, std::wstring& error) {
  auto job = std::make_shared<Job>();
  job->html = html;
  job->printer = printer;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_queue.push_back(job);
  }
  PostMessageW(g_mainWnd, WM_APP_HTMLPRINT, 0, 0);
  if (WaitForSingleObject(job->done, 120000) != WAIT_OBJECT_0) {
    error = L"Tempo esgotado aguardando a impressão HTML";
    return false;
  }
  error = job->error;
  return job->ok;
}

void htmlPrintPump() {
  if (g_current) return;                               // one job at a time
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_queue.empty()) return;
  }
  switch (g_state) {
    case State::Idle: createWebView(); if (g_state != State::Failed) return; [[fallthrough]];
    case State::Failed: {
      std::wstring why = L"WebView2 Runtime indisponível (" + winErrorText((DWORD)g_createError) + L"). Instale o Microsoft Edge WebView2 Runtime.";
      logError("WebView2 indisponível para imprimir HTML", Json::object().set("erro", why));
      failAll(why);
      g_state = State::Idle;                            // try again on the next job
      return;
    }
    case State::Creating: return;
    case State::Ready: break;
  }
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_current = g_queue.front();
    g_queue.pop_front();
  }
  startJob();
}

void htmlPrintTimeout() { finish(false, L"Tempo esgotado na impressão HTML"); }

void htmlPrintShutdown() {
  failAll(L"Aplicativo encerrando");
  if (g_current) finish(false, L"Aplicativo encerrando");
  if (g_controller) g_controller->Close();
  g_view.Reset();
  g_controller.Reset();
  g_env.Reset();
  if (g_host) DestroyWindow(g_host);
  g_host = nullptr;
}
