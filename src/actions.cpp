#include "actions.h"
#include "app.h"
#include "htmlprint.h"
#include "logger.h"
#include "pdfprint.h"
#include "printers.h"
#include "util.h"

static Json reply(const char* status, const std::string& message, const char* acao = nullptr) {
  Json r = Json::object();
  r.set("status", status);
  if (!message.empty()) r.set("message", message);
  if (acao) r.set("acao", acao);
  return r;
}

// The README documents "impresora" (typo); the old code only read "impressora". Accept both.
static std::string printerOf(const Json& req) {
  std::string p = req.str("impressora");
  return p.empty() ? req.str("impresora") : p;
}

Json printersDetailed() {
  Json data = Json::array();
  for (auto& p : listPrinters()) data.push(p.toJson());
  Json r = Json::object();
  r.set("status", "success");
  r.set("data", data);
  return r;
}

static Json todasImpressoras() {
  logInfo("Solicitação de listagem de impressoras", Json::object().set("acao", "todasImpressoras").set("tipo", "CONSULTA"));
  Json names = Json::array();
  auto list = listPrinters();
  for (auto& p : list) names.push(p.name);
  logInfo("Listagem de impressoras concluída com sucesso", Json::object().set("totalEncontradas", list.size()));
  Json r = Json::object();
  r.set("status", "success").set("acao", "todasImpressoras").set("data", names);
  return r;
}

static Json imprimirTexto(const Json& req) {
  std::string printer = printerOf(req);
  std::string msg = req.str("msg");
  logInfo("Nova solicitação de impressão recebida", Json::object().set("impressora", printer).set("size", msg.size()).set("tipo", "TEXTO RAW"));
  if (printer.empty() || msg.empty()) {
    logError("Dados obrigatórios não informados", Json::object().set("impressora", !printer.empty()).set("hasMsg", !msg.empty()));
    return reply("error", "Dados inválidos");
  }
  PrinterTarget target;
  if (!resolvePrinter(widen(printer), target)) {
    logError("Impressora não localizada", Json::object().set("impressora", printer));
    return reply("error", "Impressora não localizada");
  }
  std::wstring error, method;
  // Bytes go exactly as received (UTF-8), like the Electron version.
  if (!printRaw(target, msg, L"PrayerApp - RAW", error, method)) {
    logError("Falha de impressão RAW", Json::object().set("impressora", printer).set("queueName", target.queueName).set("destino", target.sharePath).set("erro", error));
    return reply("error", "Falha de impressão: " + narrow(error));
  }
  logInfo("Impressão concluída com sucesso", Json::object().set("impressora", printer).set("queueName", target.queueName).set("size", msg.size()).set("metodo", method).set("msg", msg));
  return reply("success", "Impresso com sucesso", "imprimir");
}

static Json imprimirHTML(const Json& req) {
  std::string printer = printerOf(req);
  std::string html = req.str("msg");
  logInfo("Solicitação de impressão HTML recebida", Json::object().set("impressora", printer).set("tamanhoConteudo", html.size()).set("tipo", "HTML"));
  std::wstring queue = widen(printer);
  PrinterTarget target;
  if (!printer.empty() && resolvePrinter(queue, target) && !target.queueName.empty()) queue = target.queueName;
  std::wstring error;
  bool ok = printHtml(html, queue, error);
  if (!ok) erro("Erro ao imprimir: " + narrow(error));
  logWrite(ok ? "info" : "error", "Impressão HTML finalizada", Json::object().set("impressora", printer).set("queueName", queue).set("status", ok ? "SUCESSO" : "FALHA").set("erro", error).set("metodo", "WebView2 Print"));
  Json r = Json::object();
  r.set("status", ok ? "success" : "error").set("message", ok ? std::string("Impresso!") : narrow(error)).set("acao", "imprimirHTML");
  return r;
}

static Json imprimirCupon(const Json& req) {
  std::string printer = printerOf(req);
  const Json* msg = req.find("msg");
  std::string b64 = msg ? msg->str("content") : "";
  if (printer.empty() || b64.empty()) {
    erro("Impressora ou msg não informados!");
    logError("Dados obrigatórios não informados para cupom", Json::object().set("impressora", !printer.empty()).set("hasContent", !b64.empty()).set("tipo", "CUPOM TÉRMICO"));
    return reply("error", "Dados inválidos");
  }
  PrinterTarget target;
  if (!resolvePrinter(widen(printer), target) || target.queueName.empty()) {
    logError("Impressora não localizada no sistema", Json::object().set("impressoraSolicitada", printer).set("tipo", "PDF"));
    return reply("error", "Impressora (fila) não encontrada");
  }
  std::string pdf;
  if (!base64Decode(b64, pdf) || pdf.size() < 5) {
    logError("Conteúdo do cupom não é base64 válido", Json::object().set("impressora", printer).set("tamanho", b64.size()));
    return reply("error", "Conteúdo base64 inválido");
  }
  std::wstring error;
  int pages = 0;
  if (!printPdf(pdf, target.queueName, error, pages)) {
    logError("Falha na impressão do cupom térmico PDF", Json::object().set("impressora", printer).set("queueName", target.queueName).set("erro", error).set("tipo", "PDF TÉRMICO"));
    return reply("error", narrow(error));
  }
  logInfo("PDF térmico enviado para fila de impressão", Json::object().set("impressora", printer).set("queueName", target.queueName).set("size", pdf.size()).set("paginas", pages).set("metodo", "Windows.Data.Pdf + GDI"));
  return reply("success", "PDF enviado para a impressora", "imprimirPDF");
}

Json handleAction(const Json& req) {
  std::string acao = req.str("acao");
  if (acao == "todasImpressoras") return todasImpressoras();
  if (acao == "imprimir") return imprimirTexto(req);
  if (acao == "imprimirHTML") return imprimirHTML(req);
  if (acao == "imprimirCupon") return imprimirCupon(req);
  if (acao == "debug/printers") {
    Json list = Json::array();
    for (auto& p : listPrinters()) list.push(p.toJson());
    wchar_t user[256] = L"";
    DWORD n = 256;
    GetUserNameW(user, &n);
    logInfo("Printers snapshot", Json::object().set("user", std::wstring(user)).set("printers", list));
    return reply("ok", "", "debug/printers");
  }
  erro("Ação não reconhecida!");
  logWarn("Ação não reconhecida pelo sistema", Json::object().set("acao", acao));
  return reply("error", "Ação não reconhecida!");
}
