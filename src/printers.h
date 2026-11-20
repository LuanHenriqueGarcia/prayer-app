#pragma once
#include <string>
#include <vector>
#include "json.h"

struct PrinterInfo {
  std::wstring name, shareName, driverName, portName;
  bool shared = false, workOffline = false;
  Json toJson() const;                             // same keys as Win32_Printer in the Electron version
};

std::vector<PrinterInfo> listPrinters();

struct PrinterTarget {
  std::wstring queueName;                          // spooler queue (may be \\server\printer for connections)
  std::wstring sharePath;                          // \\localhost\share, when the queue is shared
};
// Same lookup order as the Electron version: UNC as given, ShareName, Name, then partial match.
bool resolvePrinter(const std::wstring& hint, PrinterTarget& out);

// Sends bytes to the printer untouched (ZPL/EPL/ESC-POS). Returns false and fills `error` on failure.
bool printRaw(const PrinterTarget& target, const std::string& bytes, const std::wstring& docName, std::wstring& error, std::wstring& method);
