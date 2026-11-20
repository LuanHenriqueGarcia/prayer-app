#pragma once
#include "json.h"

// Runs one WebSocket request ({"acao": ...}) and returns the reply object.
// Same actions and reply shapes as the Electron version. Blocking; call from a worker thread.
Json handleAction(const Json& request);

// Printer list in the printer-tester format ({status, data:[{Name, ShareName, ...}]}).
Json printersDetailed();
