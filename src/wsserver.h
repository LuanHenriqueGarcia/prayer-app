#pragma once

// WebSocket server on port 8080 (all interfaces, IPv4 + IPv6, like the old `ws` server).
// Each connection gets its own thread; requests are answered in order.
bool wsStart();                                    // false when the port could not be opened
void wsStop();
bool wsRunning();
