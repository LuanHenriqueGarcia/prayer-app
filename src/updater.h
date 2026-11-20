#pragma once
#include <string>

// Auto-update from GitHub Releases (LuanHenriqueGarcia/prayer-app): the latest release must carry the
// asset "PrayerApp.exe". Its SHA-256 comes from GitHub's asset digest or a "PrayerApp.exe.sha256"
// asset, and must match before the file is used.
enum UpdateEvent { UPD_CHECKING, UPD_AVAILABLE, UPD_NONE, UPD_ERROR, UPD_DOWNLOADED };

void updaterCheck(bool manual);                    // async; events reach g_mainWnd as WM_APP_UPDATE
bool updaterBusy();
// Replaces the running exe with the downloaded one and starts it. True = caller must quit now.
bool updaterInstall(std::wstring& error);
void updaterCleanup();                             // removes leftovers of a previous update
