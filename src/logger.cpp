#include "logger.h"
#include "util.h"
#include <mutex>

static std::mutex g_logMutex;
static HWND g_listener = nullptr;
static UINT g_listenerMsg = 0;

std::wstring logDir() {
  static std::wstring dir = appTempDir();
  return dir;
}

std::wstring logFilePath() {
  SYSTEMTIME t;
  GetLocalTime(&t);
  wchar_t name[64];
  swprintf(name, 64, L"prayer-%04d-%02d-%02d.log", t.wYear, t.wMonth, t.wDay);
  return logDir() + L"\\" + name;
}

void logSetListener(HWND window, UINT message) {
  std::lock_guard<std::mutex> lock(g_logMutex);
  g_listener = window;
  g_listenerMsg = message;
}

static bool isPrintLog(const std::string& message) {
  static const wchar_t* keywords[] = {L"impressão", L"imprimir", L"print", L"copy /b", L"arquivo raw",
                                      L"comando de impressão", L"enviada com sucesso"};
  std::wstring low = lowerW(widen(message));
  for (auto k : keywords) {
    if (low.find(k) != std::wstring::npos) return true;
  }
  return false;
}

static std::string metaText(const Json& v) { return v.isStr() ? v.s : v.dump(); }

static std::string formatPrintLog(const std::string& stamp, const std::string& level, const std::string& message, const Json& meta) {
  static const std::pair<const char*, const char*> fields[] = {
      {"impressora", "🖨️  Impressora: "}, {"queueName", "📋 Fila: "},  {"size", "📏 Tamanho (bytes): "},
      {"destino", "🎯 Destino: "},        {"metodo", "⚙️  Método: "},   {"status", "✅ Resultado: "},
      {"error", "❌ Erro: "},             {"erro", "❌ Erro: "}};
  const std::string sep(80, '=');
  std::string out = "\n" + sep + "\n[" + stamp + "] [" + level + "] IMPRESSÃO\n" + sep + "\n";
  out += "📄 Ação: " + message + "\n";
  for (auto& f : fields) {
    const Json* v = meta.find(f.first);
    if (v && v->truthy()) out += std::string(f.second) + metaText(*v) + "\n";
  }
  const Json* msg = meta.find("msg");
  if (msg && msg->isStr() && !msg->s.empty() && msg->s.size() < 200) out += "📝 Conteúdo: " + msg->s.substr(0, 100) + (msg->s.size() > 100 ? "..." : "") + "\n";
  out += sep + "\n\n";
  return out;
}

void logWrite(const char* level, const std::string& message, const Json& meta) {
  SYSTEMTIME t;
  GetLocalTime(&t);
  char stamp[40];
  snprintf(stamp, sizeof stamp, "%02d/%02d/%04d %02d:%02d:%02d.%03d", t.wDay, t.wMonth, t.wYear, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
  std::string lvl(level);
  for (auto& c : lvl) c = (char)toupper((unsigned char)c);

  std::string line;
  if (meta.isObj() && isPrintLog(message)) {
    line = formatPrintLog(stamp, lvl, message, meta);
  } else {
    line = std::string("[") + stamp + "] [" + lvl + "] " + message;
    if (!meta.isNull()) line += " " + metaText(meta);
    line += "\n";
  }

  std::lock_guard<std::mutex> lock(g_logMutex);
  HANDLE f = CreateFileW(logFilePath().c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f != INVALID_HANDLE_VALUE) {
    DWORD written = 0;
    WriteFile(f, line.data(), (DWORD)line.size(), &written, nullptr);
    CloseHandle(f);
  }
  if (g_listener) {
    auto* copy = new std::wstring(widen(line));
    if (!PostMessageW(g_listener, g_listenerMsg, 0, (LPARAM)copy)) delete copy;
  }
}
