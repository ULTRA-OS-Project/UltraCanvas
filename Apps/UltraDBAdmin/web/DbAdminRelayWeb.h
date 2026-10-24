// Apps/UltraDBAdmin/web/DbAdminRelayWeb.h
// The browser build's way to its databases (Docs/Research/
// UltraDBAdminDesignProposal.md 8.1): every "relay" connection a WebSocket of
// the browser's own (Emscripten's websocket.h) to the relay the page came
// from. The page holds no password - the relay does - only the user's token,
// for the session.
//
// The calls come from the window's worker threads; the browser's WebSocket
// lives on the page's main thread, which Emscripten proxies them to, and its
// answers come back there and wake the worker waiting for each.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <string>

namespace UltraDBAdmin {

// Every relay connection through the browser's WebSocket.
void UseBrowserRelay();

// The relay this page reaches by default: the page's ?relay=... when it
// names one, else wss://<the page's host>/relay (ws:// on a page served over
// plain http, as on a developer's own computer). Call on the main thread.
std::string DefaultRelayUrl();

} // namespace UltraDBAdmin
