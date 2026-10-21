// UltraCanvas/Plugins/UltraNet/common/UltraNetPluginGuard.h
// The guard around every entry point of an UltraNet plug-in. Nothing may
// leave a plug-in as an exception: the caller is usually a worker thread with
// no handler, where std::terminate ends the whole process (SIGABRT - how a
// null subject in a JMAP answer once took UltraMail down). A body that
// throws - a JSON type the parser did not expect, a bad_alloc, anything -
// comes back as a PluginError result that names the call and the reason.
// Header-only, so every plug-in DSO carries its own copy (the plug-in import
// check stays clean).
// Version: 0.1.0 - moved here from the JMAP plug-in's JmapCore.h, for the
//                  IMAP, SMTP and POP3 plug-ins too
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <UltraNet/UltraNetCore.h>

#include <exception>
#include <string>

namespace ultranet_plugin {

// Run `body` (returning UltraNetResult) inside a handler. A result it returns
// passes through untouched; an exception becomes a PluginError
// "<what>: <reason>".
template <typename Body>
UltraNetResult Guarded(const char* what, Body&& body) {
    try {
        return body();
    } catch (const std::exception& e) {
        return UltraNetResult::Error(UltraNetResultCode::PluginError,
                                     std::string(what) + ": " + e.what());
    } catch (...) {
        return UltraNetResult::Error(UltraNetResultCode::PluginError,
                                     std::string(what) + ": an unexpected error");
    }
}

// The same for a call with nothing to return (Shutdown, a callback on a
// worker thread): an exception is swallowed. True when the body ran through.
template <typename Body>
bool GuardedVoid(Body&& body) {
    try {
        body();
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace ultranet_plugin
