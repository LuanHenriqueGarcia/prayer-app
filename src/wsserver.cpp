#include "wsserver.h"
#include "actions.h"
#include "app.h"
#include "logger.h"
#include "util.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <objbase.h>
#include <atomic>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

namespace {

const char* PORTA_OCUPADA =
    "A impressão não conseguiu iniciar: outro programa está usando a porta 8080. Feche o outro PrayerApp aberto ou reinicie o computador.";
const size_t MAX_MESSAGE = 128u * 1024 * 1024;    // PDFs in base64 can be large

std::mutex g_mutex;
SOCKET g_listen = INVALID_SOCKET;
std::set<SOCKET> g_clients;
std::atomic<bool> g_running{false};
std::atomic<int> g_clientCount{0};

void portOwners(Json& pids) {
  // Who holds port 8080 (the old version ran "netstat -ano | findstr :8080").
  ULONG size = 0;
  GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0);
  std::vector<BYTE> buf(size);
  if (GetExtendedTcpTable(buf.data(), &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0) != NO_ERROR) return;
  auto* t = (MIB_TCPTABLE_OWNER_PID*)buf.data();
  for (DWORD i = 0; i < t->dwNumEntries; i++) {
    if (ntohs((u_short)t->table[i].dwLocalPort) == SERVER_PORT) pids.push(t->table[i].dwOwningPid);
  }
}

bool sendAll(SOCKET s, const char* data, size_t len) {
  while (len) {
    int n = send(s, data, (int)std::min<size_t>(len, 1 << 20), 0);
    if (n <= 0) return false;
    data += n;
    len -= (size_t)n;
  }
  return true;
}

bool recvAll(SOCKET s, char* data, size_t len) {
  while (len) {
    int n = recv(s, data, (int)std::min<size_t>(len, 1 << 20), 0);
    if (n <= 0) return false;
    data += n;
    len -= (size_t)n;
  }
  return true;
}

bool sendFrame(SOCKET s, int opcode, const std::string& payload) {
  std::string h;
  h += (char)(0x80 | opcode);
  size_t n = payload.size();
  if (n < 126) {
    h += (char)n;
  } else if (n < 65536) {
    h += (char)126;
    h += (char)(n >> 8);
    h += (char)(n & 0xFF);
  } else {
    h += (char)127;
    for (int i = 7; i >= 0; i--) h += (char)((unsigned long long)n >> (8 * i) & 0xFF);
  }
  return sendAll(s, h.data(), h.size()) && sendAll(s, payload.data(), payload.size());
}

std::string headerValue(const std::string& req, const char* name) {
  std::string low = req;
  for (auto& c : low) c = (char)tolower((unsigned char)c);
  std::string key = std::string("\r\n") + name + ":";
  size_t p = low.find(key);
  if (p == std::string::npos) return "";
  p += key.size();
  size_t e = req.find("\r\n", p);
  std::string v = req.substr(p, e - p);
  size_t a = v.find_first_not_of(" \t"), b = v.find_last_not_of(" \t");
  return a == std::string::npos ? "" : v.substr(a, b - a + 1);
}

bool handshake(SOCKET s) {
  std::string req;
  char buf[2048];
  while (req.find("\r\n\r\n") == std::string::npos) {
    if (req.size() > 16384) return false;
    int n = recv(s, buf, sizeof buf, 0);
    if (n <= 0) return false;
    req.append(buf, n);
  }
  std::string key = headerValue(req, "sec-websocket-key");
  std::string upgrade = headerValue(req, "upgrade");
  for (auto& c : upgrade) c = (char)tolower((unsigned char)c);
  if (key.empty() || upgrade != "websocket") {
    const char* resp = "HTTP/1.1 426 Upgrade Required\r\nContent-Type: text/plain\r\nContent-Length: 16\r\nConnection: close\r\n\r\nUpgrade Required";
    sendAll(s, resp, strlen(resp));
    return false;
  }
  std::string accept = base64Encode(sha1Raw(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").data(), 20);
  std::string resp = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + accept + "\r\n\r\n";
  return sendAll(s, resp.data(), resp.size());
}

void processMessage(SOCKET s, const std::string& raw) {
  Json req;
  if (!Json::parse(raw, req) || !req.isObj()) {
    erro("JSON inválido recebido.");
    logError("Formato JSON inválido na mensagem recebida", Json::object().set("dadosBrutos", raw.substr(0, 200) + "...").set("tamanho", raw.size()));
    sendFrame(s, 1, Json::object().set("status", "error").set("message", "JSON inválido!").dump());
    return;
  }
  std::string acao = req.str("acao");
  if (acao.empty()) {
    erro("Ação não especificada.");
    logWarn("Mensagem sem ação especificada", Json::object().set("tamanhoMensagem", raw.size()));
    sendFrame(s, 1, Json::object().set("status", "error").set("message", "Ação não especificada!").dump());
    return;
  }
  logInfo("Nova requisição recebida via WebSocket", Json::object().set("acao", acao).set("impressora", req.str("impressora")).set("tamanhoMsg", req.str("msg").size()));
  Json res;
  try {
    res = handleAction(req);
  } catch (const std::exception& e) {
    logError("Falha crítica no processamento da ação", Json::object().set("acao", acao).set("erro", e.what()));
    res = Json::object().set("status", "error").set("message", e.what());
  }
  std::string out = res.dump();
  logInfo("Resposta enviada ao cliente", Json::object().set("acao", acao).set("status", res.str("status")).set("tamanhoResposta", out.size()));
  sendFrame(s, 1, out);
}

void clientThread(SOCKET s) {
  HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (handshake(s)) {
    int count = ++g_clientCount;
    sucesso("Cliente conectado!");
    logInfo("Nova conexão WebSocket estabelecida", Json::object().set("clientesConectados", count));

    std::string message;
    int messageOp = 0;
    for (;;) {
      unsigned char h[2];
      if (!recvAll(s, (char*)h, 2)) break;
      bool fin = h[0] & 0x80;
      int op = h[0] & 0x0F;
      bool masked = h[1] & 0x80;
      unsigned long long len = h[1] & 0x7F;
      if (len == 126) {
        unsigned char e[2];
        if (!recvAll(s, (char*)e, 2)) break;
        len = ((unsigned)e[0] << 8) | e[1];
      } else if (len == 127) {
        unsigned char e[8];
        if (!recvAll(s, (char*)e, 8)) break;
        len = 0;
        for (int i = 0; i < 8; i++) len = (len << 8) | e[i];
      }
      if (!masked || len > MAX_MESSAGE || message.size() + len > MAX_MESSAGE) {
        sendFrame(s, 8, std::string("\x03\xF1", 2));   // 1009 / protocol error: close
        break;
      }
      unsigned char mask[4];
      if (!recvAll(s, (char*)mask, 4)) break;
      std::string payload((size_t)len, '\0');
      if (len && !recvAll(s, payload.data(), (size_t)len)) break;
      for (size_t i = 0; i < payload.size(); i++) payload[i] ^= mask[i & 3];

      if (op == 8) { sendFrame(s, 8, payload.substr(0, 2)); break; }
      if (op == 9) { sendFrame(s, 10, payload); continue; }
      if (op == 10) continue;
      if (op == 1 || op == 2) { message = std::move(payload); messageOp = op; }
      else if (op == 0 && messageOp) message += payload;
      else break;
      if (!fin) continue;
      processMessage(s, message);
      message.clear();
      messageOp = 0;
    }
    int left = --g_clientCount;
    if (g_running) alerta("Cliente desconectado.");
    logInfo("Cliente desconectado do WebSocket", Json::object().set("clientesRestantes", left));
  }
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_clients.erase(s);
  }
  shutdown(s, SD_BOTH);
  closesocket(s);
  if (SUCCEEDED(co)) CoUninitialize();
}

void acceptThread(SOCKET listener) {
  for (;;) {
    SOCKET c = accept(listener, nullptr, nullptr);
    if (c == INVALID_SOCKET) break;                // listener closed by wsStop()
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_running) { closesocket(c); break; }
    g_clients.insert(c);
    std::thread(clientThread, c).detach();
  }
}

}  // namespace

bool wsStart() {
  if (g_running) {
    sucesso("Servidor já em execução.");
    return true;
  }
  static bool wsaReady = false;
  if (!wsaReady) {
    WSADATA wsa;
    wsaReady = WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
  }

  DWORD off = 0, on = 1;
  sockaddr_in6 addr6{};
  addr6.sin6_family = AF_INET6;
  addr6.sin6_addr = in6addr_any;
  addr6.sin6_port = htons(SERVER_PORT);
  sockaddr_in addr4{};
  addr4.sin_family = AF_INET;
  addr4.sin_addr.s_addr = htonl(INADDR_ANY);
  addr4.sin_port = htons(SERVER_PORT);
  sockaddr* addr = (sockaddr*)&addr6;
  int addrLen = sizeof addr6;

  SOCKET s = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
  if (s != INVALID_SOCKET) {
    setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&off, sizeof off);   // also IPv4 (127.0.0.1)
  } else {
    s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);                             // IPv6 disabled on this PC
    addr = (sockaddr*)&addr4;
    addrLen = sizeof addr4;
  }
  if (s == INVALID_SOCKET) {
    logError("Erro inesperado ao criar servidor WebSocket", Json::object().set("erro", winErrorText(WSAGetLastError())));
    erro("A impressão não conseguiu iniciar. Reinicie o PrayerApp ou o computador.");
    return false;
  }
  setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&on, sizeof on);
  if (bind(s, addr, addrLen) != 0 || listen(s, SOMAXCONN) != 0) {
    int err = WSAGetLastError();
    closesocket(s);
    if (err == WSAEADDRINUSE || err == WSAEACCES) {
      Json pids = Json::array();
      portOwners(pids);
      erro(PORTA_OCUPADA);
      logError("Falha ao iniciar WebSocket: porta ocupada", Json::object().set("porta", SERVER_PORT).set("erro", winErrorText(err)).set("pids", pids)
                   .set("dica", "Verifique se outra instância do Prayer está aberta ou finalize o processo que usa a porta."));
    } else {
      erro("A impressão não conseguiu iniciar. Reinicie o PrayerApp ou o computador.");
      logError("Falha no servidor WebSocket", Json::object().set("porta", SERVER_PORT).set("erro", winErrorText(err)));
    }
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_listen = s;
    g_running = true;
  }
  std::thread(acceptThread, s).detach();
  sucesso("Servidor WebSocket iniciado na porta 8080.");
  logInfo("Sistema WebSocket totalmente operacional", Json::object().set("porta", SERVER_PORT).set("status", "PRONTO_PARA_CONEXÕES"));
  return true;
}

void wsStop() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_running) return;
  g_running = false;
  closesocket(g_listen);
  g_listen = INVALID_SOCKET;
  for (SOCKET c : g_clients) shutdown(c, SD_BOTH);   // client threads see the error and close
  logWarn("Servidor WebSocket finalizado", Json::object().set("motivo", "STOP_REQUEST"));
}

bool wsRunning() { return g_running; }
