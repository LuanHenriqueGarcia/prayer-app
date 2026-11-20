#pragma once
#include <windows.h>
#include <string>
#include "json.h"

// File logger: %TEMP%\prayer\prayer-YYYY-MM-DD.log (same place and format as the Electron version).
void logWrite(const char* level, const std::string& message, const Json& meta = Json());
inline void logInfo(const std::string& m, const Json& meta = Json()) { logWrite("info", m, meta); }
inline void logWarn(const std::string& m, const Json& meta = Json()) { logWrite("warn", m, meta); }
inline void logError(const std::string& m, const Json& meta = Json()) { logWrite("error", m, meta); }

std::wstring logDir();
std::wstring logFilePath();                       // today's file

// The log viewer registers its window here; each new entry is posted to it as
// (message, 0, LPARAM = new std::wstring) and the receiver deletes the string.
void logSetListener(HWND window, UINT message);
