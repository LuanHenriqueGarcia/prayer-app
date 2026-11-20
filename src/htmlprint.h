#pragma once
#include <windows.h>
#include <string>

// Silent HTML printing through one hidden WebView2 (Edge engine already on Windows 10/11).
// printHtml() can be called from any thread except the UI one: it queues the job, wakes the
// main window (WM_APP_HTMLPRINT) and waits for the result.
bool printHtml(const std::string& htmlUtf8, const std::wstring& printerName, std::wstring& error);

// UI thread side.
void htmlPrintPump();                              // on WM_APP_HTMLPRINT
void htmlPrintTimeout();                           // on WM_TIMER TIMER_HTMLPRINT
void htmlPrintShutdown();
