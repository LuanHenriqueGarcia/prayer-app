#include "printers.h"
#include "util.h"
#include <windows.h>
#include <winspool.h>

Json PrinterInfo::toJson() const {
  Json j = Json::object();
  j.set("Name", name);
  j.set("ShareName", shareName.empty() ? Json() : Json(shareName));
  j.set("DriverName", driverName);
  j.set("PortName", portName);
  j.set("Shared", shared);
  j.set("WorkOffline", workOffline);
  return j;
}

std::vector<PrinterInfo> listPrinters() {
  std::vector<PrinterInfo> out;
  const DWORD flags = PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS;
  DWORD needed = 0, count = 0;
  EnumPrintersW(flags, nullptr, 2, nullptr, 0, &needed, &count);
  if (!needed) return out;
  std::vector<BYTE> buf(needed);
  if (!EnumPrintersW(flags, nullptr, 2, buf.data(), needed, &needed, &count)) return out;
  auto* p = (PRINTER_INFO_2W*)buf.data();
  for (DWORD i = 0; i < count; i++) {
    PrinterInfo info;
    info.name = p[i].pPrinterName ? p[i].pPrinterName : L"";
    info.shareName = p[i].pShareName ? p[i].pShareName : L"";
    info.driverName = p[i].pDriverName ? p[i].pDriverName : L"";
    info.portName = p[i].pPortName ? p[i].pPortName : L"";
    info.shared = (p[i].Attributes & PRINTER_ATTRIBUTE_SHARED) != 0;
    info.workOffline = (p[i].Attributes & PRINTER_ATTRIBUTE_WORK_OFFLINE) != 0;
    if (!info.name.empty()) out.push_back(std::move(info));
  }
  return out;
}

static std::wstring sanitizeHint(std::wstring s) {
  for (auto prefix : {L"\\\\localhost\\", L"\\\\127.0.0.1\\"}) {
    size_t n = wcslen(prefix);
    if (s.size() > n && lowerW(s.substr(0, n)) == prefix) { s = s.substr(n); break; }
  }
  std::wstring clean;
  for (wchar_t c : s) {
    if (c == 0xA0) clean += L' ';
    else if (c >= 0x20 && c != 0x7F) clean += c;
  }
  size_t a = clean.find_first_not_of(L' '), b = clean.find_last_not_of(L' ');
  return a == std::wstring::npos ? L"" : clean.substr(a, b - a + 1);
}

bool resolvePrinter(const std::wstring& hintRaw, PrinterTarget& out) {
  if (hintRaw.empty()) return false;
  auto printers = listPrinters();
  std::wstring hint = sanitizeHint(hintRaw);
  std::wstring low = lowerW(hint);

  auto fill = [&](const PrinterInfo& p) {
    out.queueName = p.name;
    out.sharePath = p.shareName.empty() ? L"" : L"\\\\localhost\\" + p.shareName;
    return true;
  };

  // A UNC name that is not a local share: it is a printer connection (\\server\printer).
  if (hintRaw.rfind(L"\\\\", 0) == 0 && hint == hintRaw) {
    for (auto& p : printers) {
      if (lowerW(p.name) == low) return fill(p);
    }
    out.queueName = hintRaw;
    out.sharePath = hintRaw;
    return true;
  }
  for (auto& p : printers) {
    if (!p.shareName.empty() && lowerW(p.shareName) == low) return fill(p);
  }
  for (auto& p : printers) {
    if (lowerW(p.name) == low) return fill(p);
  }
  for (auto& p : printers) {
    if (lowerW(p.name).find(low) != std::wstring::npos || lowerW(p.shareName).find(low) != std::wstring::npos) return fill(p);
  }
  return false;
}

static bool spoolRaw(const std::wstring& queue, const std::string& bytes, const std::wstring& docName, const wchar_t* datatype, DWORD& err) {
  HANDLE h = nullptr;
  if (!OpenPrinterW((LPWSTR)queue.c_str(), &h, nullptr)) { err = GetLastError(); return false; }
  DOC_INFO_1W doc{};
  doc.pDocName = (LPWSTR)docName.c_str();
  doc.pDatatype = (LPWSTR)datatype;
  bool ok = false;
  err = 0;
  if (StartDocPrinterW(h, 1, (LPBYTE)&doc)) {
    if (StartPagePrinter(h)) {
      DWORD written = 0;
      ok = WritePrinter(h, (LPVOID)bytes.data(), (DWORD)bytes.size(), &written) && written == bytes.size();
      if (!ok) err = GetLastError();
      EndPagePrinter(h);
    } else {
      err = GetLastError();
    }
    if (!EndDocPrinter(h) && ok) { ok = false; err = GetLastError(); }
  } else {
    err = GetLastError();
  }
  ClosePrinter(h);
  return ok;
}

static bool writeToPath(const std::wstring& path, const std::string& bytes, DWORD& err) {
  HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  if (f == INVALID_HANDLE_VALUE) { err = GetLastError(); return false; }
  DWORD written = 0;
  bool ok = WriteFile(f, bytes.data(), (DWORD)bytes.size(), &written, nullptr) && written == bytes.size();
  err = ok ? 0 : GetLastError();
  CloseHandle(f);
  return ok;
}

bool printRaw(const PrinterTarget& t, const std::string& bytes, const std::wstring& docName, std::wstring& error, std::wstring& method) {
  DWORD err = 0;
  if (!t.queueName.empty()) {
    // RAW straight to the spooler; v4 (XPS) drivers refuse RAW and accept XPS_PASS instead.
    for (auto dt : {L"RAW", L"XPS_PASS"}) {
      if (spoolRaw(t.queueName, bytes, docName, dt, err)) {
        method = std::wstring(L"spooler ") + dt;
        return true;
      }
      if (err != ERROR_INVALID_DATATYPE) break;
    }
    error = L"Spooler: " + winErrorText(err);
  }
  // Last resort, what the Electron version did with "copy /b": write to the share.
  if (!t.sharePath.empty()) {
    if (writeToPath(t.sharePath, bytes, err)) {
      method = L"compartilhamento " + t.sharePath;
      return true;
    }
    error += (error.empty() ? L"" : L" | ") + std::wstring(L"Compartilhamento: ") + winErrorText(err);
  }
  if (error.empty()) error = L"Impressora sem fila nem compartilhamento";
  return false;
}
