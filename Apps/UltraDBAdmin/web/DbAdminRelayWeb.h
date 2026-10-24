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
// Version: 0.2.0 - the relay's sign-in: sent there, the ticket taken back
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

// The relay's sign-in (Apps/UltraDBAdmin/relay/RelaySignIn.h): the page goes
// to <relay>/signin, and the relay brings it back to this address with a
// ticket in the fragment. Leaves the page.
void StartRelaySignIn(const std::string& relayUrl);

// The ticket a sign-in brought back (#relay_ticket=...), taken off the
// page's address so neither history nor a bookmark keeps it; "" for none.
std::string TakeRelayTicket();

} // namespace UltraDBAdmin
