#include "updater.h"
#include "app.h"
#include "json.h"
#include "logger.h"
#include "util.h"
#include <winhttp.h>
#include <atomic>
#include <thread>

namespace {

const wchar_t* REPO_PATH = L"/repos/LuanHenriqueGarcia/prayer-app/releases/latest";
const char* ASSET = "PrayerApp.exe";
std::atomic<bool> g_busy{false};
std::wstring g_downloaded;                         // path of the verified new exe

void post(UpdateEvent ev, const std::wstring& detail = L"") {
  PostMessageW(g_mainWnd, WM_APP_UPDATE, (WPARAM)ev, (LPARAM) new std::wstring(detail));
}

std::wstring sibling(const wchar_t* name) {
  std::wstring p = exePath();
  return p.substr(0, p.find_last_of(L'\\') + 1) + name;
}

// GET over HTTPS (redirects followed). Body goes to `body` or, when `file` is set, to that file.
bool httpGet(const std::wstring& url, std::string* body, const std::wstring* file, std::wstring& error) {
  URL_COMPONENTS uc{sizeof uc};
  wchar_t host[256], path[2048];
  uc.lpszHostName = host;
  uc.dwHostNameLength = 256;
  uc.lpszUrlPath = path;
  uc.dwUrlPathLength = 2048;
  wchar_t extra[2048];
  uc.lpszExtraInfo = extra;
  uc.dwExtraInfoLength = 2048;
  if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS) {
    error = L"URL inválida";
    return false;
  }
  std::wstring agent = std::wstring(L"PrayerApp/") + APP_VERSION_W;
  HINTERNET ses = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!ses) ses = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  HINTERNET con = ses ? WinHttpConnect(ses, host, uc.nPort, 0) : nullptr;
  std::wstring target = std::wstring(path) + extra;
  HINTERNET req = con ? WinHttpOpenRequest(con, L"GET", target.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
  bool ok = false;
  HANDLE out = INVALID_HANDLE_VALUE;
  if (req) WinHttpSetTimeouts(req, 15000, 15000, 30000, 60000);
  const wchar_t* headers = L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
  if (req && WinHttpSendRequest(req, body ? headers : WINHTTP_NO_ADDITIONAL_HEADERS, body ? (DWORD)-1L : 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
      WinHttpReceiveResponse(req, nullptr)) {
    DWORD status = 0, sz = sizeof status;
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &sz, nullptr);
    if (status != 200) {
      error = L"HTTP " + std::to_wstring(status);
    } else {
      if (file) out = CreateFileW(file->c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
      if (file && out == INVALID_HANDLE_VALUE) {
        error = L"Não foi possível gravar " + *file + L": " + winErrorText(GetLastError());
      } else {
        ok = true;
        std::string chunk;
        for (;;) {
          DWORD avail = 0;
          if (!WinHttpQueryDataAvailable(req, &avail)) { ok = false; break; }
          if (!avail) break;
          chunk.resize(avail);
          DWORD got = 0;
          if (!WinHttpReadData(req, chunk.data(), avail, &got)) { ok = false; break; }
          if (body) body->append(chunk.data(), got);
          DWORD w = 0;
          if (file && (!WriteFile(out, chunk.data(), got, &w, nullptr) || w != got)) { ok = false; break; }
        }
        if (!ok) error = L"Download interrompido: " + winErrorText(GetLastError());
      }
    }
  } else {
    error = winErrorText(GetLastError());
  }
  if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
  if (req) WinHttpCloseHandle(req);
  if (con) WinHttpCloseHandle(con);
  if (ses) WinHttpCloseHandle(ses);
  return ok;
}

bool parseVersion(std::string v, int out[3]) {
  if (!v.empty() && (v[0] == 'v' || v[0] == 'V')) v.erase(0, 1);
  return sscanf(v.c_str(), "%d.%d.%d", &out[0], &out[1], &out[2]) == 3;
}

bool isNewer(const std::string& tag) {
  int a[3], b[3] = {APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_VERSION_PATCH};
  if (!parseVersion(tag, a)) return false;
  for (int i = 0; i < 3; i++) {
    if (a[i] != b[i]) return a[i] > b[i];
  }
  return false;
}

void run(bool manual) {
  post(UPD_CHECKING);
  std::wstring error;
  std::string body;
  if (!httpGet(std::wstring(L"https://api.github.com") + REPO_PATH, &body, nullptr, error)) {
    post(UPD_ERROR, L"Falha ao consultar o GitHub: " + error);
    return;
  }
  Json rel;
  if (!Json::parse(body, rel)) {
    post(UPD_ERROR, L"Resposta inválida do GitHub");
    return;
  }
  std::string tag = rel.str("tag_name");
  if (!isNewer(tag)) {
    post(UPD_NONE, widen(tag));
    return;
  }
  const Json* assets = rel.find("assets");
  std::string exeUrl, shaUrl, digest;
  double exeSize = 0;
  if (assets) {
    for (auto& a : assets->a) {
      std::string name = a.str("name");
      if (name == ASSET) {
        exeUrl = a.str("browser_download_url");
        digest = a.str("digest");
        if (const Json* s = a.find("size")) exeSize = s->n;
      } else if (name == std::string(ASSET) + ".sha256") {
        shaUrl = a.str("browser_download_url");
      }
    }
  }
  if (exeUrl.empty()) {
    logWarn("Release mais nova sem o PrayerApp.exe nativo", Json::object().set("tag", tag));
    post(UPD_NONE, widen(tag));
    return;
  }
  std::string expected;
  if (digest.rfind("sha256:", 0) == 0) expected = digest.substr(7);
  if (expected.empty() && !shaUrl.empty()) {
    std::string txt;
    if (httpGet(widen(shaUrl), &txt, nullptr, error)) expected = txt.substr(0, txt.find_first_of(" \t\r\n"));
  }
  for (auto& c : expected) c = (char)tolower((unsigned char)c);
  if (expected.size() != 64) {
    post(UPD_ERROR, L"Release " + widen(tag) + L" sem SHA-256 do PrayerApp.exe; atualização recusada");
    return;
  }

  post(UPD_AVAILABLE, widen(tag));
  std::wstring dest = sibling(L"PrayerApp.new.exe");
  if (!httpGet(widen(exeUrl), nullptr, &dest, error)) {
    DeleteFileW(dest.c_str());
    post(UPD_ERROR, L"Falha ao baixar a atualização: " + error);
    return;
  }
  std::string got;
  WIN32_FILE_ATTRIBUTE_DATA fa{};
  GetFileAttributesExW(dest.c_str(), GetFileExInfoStandard, &fa);
  double size = (double)(((ULONGLONG)fa.nFileSizeHigh << 32) | fa.nFileSizeLow);
  if (!sha256HexOfFile(dest, got) || got != expected || (exeSize > 0 && size != exeSize)) {
    DeleteFileW(dest.c_str());
    logError("Atualização com hash diferente do publicado", Json::object().set("esperado", expected).set("obtido", got));
    post(UPD_ERROR, L"Arquivo baixado não confere com o SHA-256 publicado; descartado");
    return;
  }
  g_downloaded = dest;
  post(UPD_DOWNLOADED, widen(tag));
  (void)manual;
}

}  // namespace

void updaterCheck(bool manual) {
  if (g_busy.exchange(true)) return;
  std::thread([manual] {
    run(manual);
    g_busy = false;
  }).detach();
}

bool updaterBusy() { return g_busy; }

bool updaterInstall(std::wstring& error) {
  if (g_downloaded.empty()) {
    error = L"Nenhuma atualização baixada";
    return false;
  }
  std::wstring self = exePath();
  std::wstring old = sibling(L"PrayerApp.old.exe");
  DeleteFileW(old.c_str());
  // A running exe cannot be overwritten but can be renamed.
  if (!MoveFileExW(self.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) {
    error = L"Não foi possível trocar o executável: " + winErrorText(GetLastError());
    return false;
  }
  if (!MoveFileExW(g_downloaded.c_str(), self.c_str(), MOVEFILE_REPLACE_EXISTING)) {
    error = L"Não foi possível instalar a nova versão: " + winErrorText(GetLastError());
    MoveFileExW(old.c_str(), self.c_str(), MOVEFILE_REPLACE_EXISTING);
    return false;
  }
  std::wstring cmd = L"\"" + self + L"\" --after-update " + std::to_wstring(GetCurrentProcessId());
  STARTUPINFOW si{sizeof si};
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(self.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
    error = L"Nova versão instalada, mas não abriu: " + winErrorText(GetLastError());
    MoveFileExW(self.c_str(), g_downloaded.c_str(), MOVEFILE_REPLACE_EXISTING);
    MoveFileExW(old.c_str(), self.c_str(), MOVEFILE_REPLACE_EXISTING);
    return false;
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return true;
}

void updaterCleanup() {
  std::wstring old = sibling(L"PrayerApp.old.exe");
  for (int i = 0; i < 20 && GetFileAttributesW(old.c_str()) != INVALID_FILE_ATTRIBUTES; i++) {
    if (DeleteFileW(old.c_str())) break;
    Sleep(250);
  }
  DeleteFileW(sibling(L"PrayerApp.new.exe").c_str());
}
