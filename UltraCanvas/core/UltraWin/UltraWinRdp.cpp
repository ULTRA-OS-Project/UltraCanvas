// core/UltraWin/UltraWinRdp.cpp
// libfreerdp2-backed implementation of RdpSession. Follows the standard
// embedded-client shape: freerdp_new + settings via the typed settings
// API, blocking freerdp_connect, then a pump thread over
// freerdp_get_event_handles / freerdp_check_event_handles until the peer
// closes or Disconnect() is called. Graphics land in FreeRDP's software
// GDI surface; the Stage 2b element consumes them from there.
// When built without FreeRDP the whole implementation degrades to
// RdpBuiltIn() == false and failing sessions.
// Version: 0.1.0 (Stage 2b)
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraWinRdp.h"

#ifdef ULTRAWIN_HAS_FREERDP

#include <freerdp/freerdp.h>
#include <freerdp/client/channels.h>
#include <freerdp/client/cmdline.h>
#include <freerdp/gdi/gdi.h>
#include <winpr/synch.h>

// The 2.x/3.x API differences the session touches, folded into two
// helpers: settings live on the instance in 2.x but on the context in
// 3.x, and the shall-disconnect check moved onto the context.
namespace {

rdpSettings* SessionSettings(freerdp* instance) {
#if ULTRAWIN_HAS_FREERDP >= 3
    return instance->context->settings;
#else
    return instance->settings;
#endif
}

BOOL SessionShallDisconnect(freerdp* instance) {
#if ULTRAWIN_HAS_FREERDP >= 3
    return freerdp_shall_disconnect_context(instance->context);
#else
    return freerdp_shall_disconnect(instance);
#endif
}

}  // namespace

namespace ultrawin_internal {

bool RdpBuiltIn() { return true; }

namespace {

// Static channel machinery (RAIL rides on it in RemoteApp mode). 3.x
// calls this through the dedicated LoadChannels slot; 2.x from PreConnect.
BOOL SessionLoadChannels(freerdp* instance) {
    return freerdp_client_load_addins(instance->context->channels,
                                      SessionSettings(instance));
}

BOOL SessionPreConnect(freerdp* instance) {
#if ULTRAWIN_HAS_FREERDP >= 3
    (void)instance;
    return TRUE;
#else
    return SessionLoadChannels(instance);
#endif
}

BOOL SessionPostConnect(freerdp* instance) {
    // Software GDI: the guest's output composes into a local BGRA surface.
    return gdi_init(instance, PIXEL_FORMAT_BGRA32);
}

void SessionPostDisconnect(freerdp* instance) { gdi_free(instance); }

// The guest is reached over loopback (QEMU hostfwd) — its self-signed
// certificate is meaningless to a trust store, so accept it.
DWORD SessionVerifyCertificate(freerdp* /*instance*/, const char* /*host*/,
                               UINT16 /*port*/, const char* /*common_name*/,
                               const char* /*subject*/,
                               const char* /*issuer*/,
                               const char* /*fingerprint*/, DWORD /*flags*/) {
    return 2;  // accept for this session only (no store write)
}

}  // namespace

RdpSession::~RdpSession() { Disconnect(); }

RdpSessionState RdpSession::State() const {
    return static_cast<RdpSessionState>(state.load());
}

bool RdpSession::Connect(const RdpSessionOptions& options) {
    freerdp* rdp = freerdp_new();
    if (!rdp) {
        lastError = "freerdp_new failed";
        state = static_cast<int>(RdpSessionState::Failed);
        return false;
    }
    rdp->PreConnect = SessionPreConnect;
    rdp->PostConnect = SessionPostConnect;
    rdp->PostDisconnect = SessionPostDisconnect;
    rdp->VerifyCertificateEx = SessionVerifyCertificate;
#if ULTRAWIN_HAS_FREERDP >= 3
    rdp->LoadChannels = SessionLoadChannels;
#endif
    if (!freerdp_context_new(rdp)) {
        freerdp_free(rdp);
        lastError = "freerdp_context_new failed";
        state = static_cast<int>(RdpSessionState::Failed);
        return false;
    }
    rdpSettings* s = SessionSettings(rdp);
    freerdp_settings_set_string(s, FreeRDP_ServerHostname,
                                options.host.c_str());
    freerdp_settings_set_uint32(s, FreeRDP_ServerPort,
                                static_cast<UINT32>(options.port));
    freerdp_settings_set_string(s, FreeRDP_Username,
                                options.username.c_str());
    freerdp_settings_set_string(s, FreeRDP_Password,
                                options.password.c_str());
    freerdp_settings_set_bool(s, FreeRDP_IgnoreCertificate, TRUE);
    freerdp_settings_set_bool(s, FreeRDP_SupportGraphicsPipeline, FALSE);
    // No rdpdr drive redirection: shared folders come from virtiofs, and
    // distro FreeRDP packages don't always ship the rdpdr channel plugin
    // (its absence would fail the whole connect).
    freerdp_settings_set_bool(s, FreeRDP_DeviceRedirection, FALSE);
    freerdp_settings_set_bool(s, FreeRDP_RedirectDrives, FALSE);
    if (options.remoteApp) {
        freerdp_settings_set_bool(s, FreeRDP_RemoteApplicationMode, TRUE);
        freerdp_settings_set_bool(s, FreeRDP_RemoteAppLanguageBarSupported,
                                  TRUE);
        freerdp_settings_set_string(s, FreeRDP_RemoteApplicationProgram,
                                    options.remoteAppProgram.c_str());
        if (!options.remoteAppArgs.empty())
            freerdp_settings_set_string(s, FreeRDP_RemoteApplicationCmdLine,
                                        options.remoteAppArgs.c_str());
    }

    instance = rdp;
    if (!freerdp_connect(rdp)) {
        UINT32 err = freerdp_get_last_error(rdp->context);
        lastError = std::string("RDP connect failed: ") +
                    freerdp_get_last_error_string(err);
        freerdp_context_free(rdp);
        freerdp_free(rdp);
        instance = nullptr;
        state = static_cast<int>(RdpSessionState::Failed);
        return false;
    }
    state = static_cast<int>(RdpSessionState::Connected);
    stopRequested = false;
    pump = std::thread([this]() { PumpLoop(); });
    return true;
}

void RdpSession::PumpLoop() {
    freerdp* rdp = static_cast<freerdp*>(instance);
    HANDLE handles[64];
    while (!stopRequested.load()) {
        DWORD n =
            freerdp_get_event_handles(rdp->context, handles, 64);
        if (n == 0) break;
        DWORD wait = WaitForMultipleObjects(n, handles, FALSE, 250);
        if (wait == WAIT_FAILED) break;
        if (!freerdp_check_event_handles(rdp->context)) break;
        if (SessionShallDisconnect(rdp)) break;
    }
    state = static_cast<int>(RdpSessionState::Disconnected);
}

void RdpSession::Disconnect() {
    if (!instance) return;
    stopRequested = true;
    if (pump.joinable()) pump.join();
    freerdp* rdp = static_cast<freerdp*>(instance);
    freerdp_disconnect(rdp);
    freerdp_context_free(rdp);
    freerdp_free(rdp);
    instance = nullptr;
    if (state.load() != static_cast<int>(RdpSessionState::Failed))
        state = static_cast<int>(RdpSessionState::Disconnected);
}

}  // namespace ultrawin_internal

#else  // !ULTRAWIN_HAS_FREERDP

namespace ultrawin_internal {

bool RdpBuiltIn() { return false; }

RdpSession::~RdpSession() = default;

RdpSessionState RdpSession::State() const {
    return static_cast<RdpSessionState>(state.load());
}

bool RdpSession::Connect(const RdpSessionOptions&) {
    lastError = "UltraWin was built without FreeRDP";
    state = static_cast<int>(RdpSessionState::Failed);
    return false;
}

void RdpSession::Disconnect() {}
void RdpSession::PumpLoop() {}

}  // namespace ultrawin_internal

#endif  // ULTRAWIN_HAS_FREERDP
