#pragma once
#include <windows.h>
#include <string>
#include "version.h"

#define WM_APP_TRAY (WM_APP + 1)        // tray icon callback
#define WM_APP_NOTIFY (WM_APP + 2)      // lParam = new NoticeMsg (balloon request from any thread)
#define WM_APP_HTMLPRINT (WM_APP + 3)   // an HTML print job was queued
#define WM_APP_UPDATE (WM_APP + 4)      // wParam = UpdateEvent, lParam = new std::wstring (detail) or 0
#define WM_APP_LOGLINE (WM_APP + 5)     // to the log viewer: lParam = new std::wstring
#define WM_APP_TESTER_DONE (WM_APP + 6) // to the tester: lParam = new std::string (JSON result)

#define TIMER_HTMLPRINT 2
#define SERVER_PORT 8080

extern HINSTANCE g_inst;
extern HWND g_mainWnd;

enum class Notice { Ok, Error, Warn, Info };
// Tray balloon. Safe from any thread.
void notify(Notice kind, const std::wstring& title, const std::wstring& text);
// Same helpers the Electron version had: balloon + log entry. UTF-8 text.
void sucesso(const std::string& text);
void erro(const std::string& text);
void alerta(const std::string& text);

// Small shared UI helpers.
HFONT uiFont();
int dpi(HWND wnd, int px);
HWND makeControl(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int id, DWORD exStyle = 0);

void openLogViewer();
bool logViewerPreTranslate(MSG* msg);
void openTester();
bool testerPreTranslate(MSG* msg);
