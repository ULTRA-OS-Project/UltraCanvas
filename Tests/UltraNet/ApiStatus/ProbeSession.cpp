// Tests/UltraNet/ApiStatus/ProbeSession.cpp
// Probes for UltraNet/UltraNetCookies.h — the session surface (cookie jar,
// connection reuse, default headers) built on libcurl's share interface.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "ApiStatus.h"
#include "ProbeServer.h"

#include <UltraNet/UltraNetCookies.h>
#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetHttp.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace ultranet_apistatus;

namespace {

constexpr const char* kArea = "Session";

std::filesystem::path ScratchDir() {
    std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "ultranet_apistatus";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::string ReadWholeFile(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream os;
    os << in.rdbuf();
    return os.str();
}

// RAII so a probe that returns early through PROBE_EXPECT never leaks a session.
struct SessionGuard {
    UltraNetHandle handle = UltraNetInvalidHandle;
    explicit SessionGuard(UltraNetHandle h) : handle(h) {}
    ~SessionGuard() { if (handle != UltraNetInvalidHandle) UltraNet_DestroySession(handle); }
    SessionGuard(const SessionGuard&) = delete;
    SessionGuard& operator=(const SessionGuard&) = delete;
};

} // namespace

ULTRANET_PROBE(kArea, UltraNet_CreateSession) {
    UltraNetSessionOptions opt;
    opt.reuseConnections = true;
    opt.defaultHeaders.Set("X-Probe", "session-default");

    SessionGuard s(UltraNet_CreateSession(opt));
    PROBE_EXPECT(s.handle != UltraNetInvalidHandle);

    LoopbackServer& server = Loopback();
    if (!server.Running()) {
        return Implemented("session handle allocated, but no loopback origin to "
                           "confirm it carries state: " + server.Error());
    }

    // Default headers are the observable proof the session options took effect.
    UltraNetResponse resp;
    const UltraNetResult r = UltraNet_SessionHttpGet(s.handle, server.Url("/echo"), resp);
    PROBE_EXPECT_MSG(static_cast<bool>(r), r.message);
    PROBE_EXPECT(resp.headers.Get("x-echo-probe") == "session-default");
    return Working("session created; UltraNetSessionOptions::defaultHeaders "
                   "reached the origin");
}

ULTRANET_PROBE(kArea, UltraNet_DestroySession) {
    const UltraNetHandle h = UltraNet_CreateSession();
    PROBE_EXPECT(h != UltraNetInvalidHandle);
    UltraNet_DestroySession(h);

    // Using a destroyed session must fail as an invalid handle, not crash.
    UltraNetResponse resp;
    const UltraNetResult after = UltraNet_SessionHttpGet(h, "http://127.0.0.1/", resp);
    PROBE_EXPECT(!after);
    PROBE_EXPECT(after.code == UltraNetResultCode::InvalidHandle);

    UltraNet_DestroySession(h);          // double destroy must be a no-op
    UltraNet_DestroySession(UltraNetInvalidHandle);
    return Working("handle retired; later use reports InvalidHandle; repeat "
                   "destroy is a safe no-op");
}

ULTRANET_PROBE(kArea, UltraNet_SessionHttpGet) {
    LoopbackServer& server = Loopback();

    UltraNetResponse ignored;
    const UltraNetResult noSession =
        UltraNet_SessionHttpGet(999999, "http://127.0.0.1/", ignored);
    PROBE_EXPECT(!noSession && noSession.code == UltraNetResultCode::InvalidHandle);

    if (!server.Running()) {
        return Implemented("unknown session rejected, but no loopback origin "
                           "for a live session GET: " + server.Error());
    }

    SessionGuard s(UltraNet_CreateSession());
    PROBE_EXPECT(s.handle != UltraNetInvalidHandle);

    // Cookie continuity across two GETs is what makes this a *session*.
    UltraNetResponse first;
    const UltraNetResult r1 = UltraNet_SessionHttpGet(
        s.handle, server.Url("/setcookie"), first);
    PROBE_EXPECT_MSG(static_cast<bool>(r1), r1.message);
    PROBE_EXPECT(first.headers.Get("Set-Cookie").find("ultranet_probe") !=
                 std::string::npos);

    UltraNetResponse second;
    const UltraNetResult r2 = UltraNet_SessionHttpGet(
        s.handle, server.Url("/cookie"), second);
    PROBE_EXPECT_MSG(static_cast<bool>(r2), r2.message);
    PROBE_EXPECT_MSG(second.GetBodyAsString() == "cookie=ultranet_probe=chocolate",
                     "second request sent \"" + second.GetBodyAsString() + "\"");
    return Working("cookie set on the first GET was replayed on the second "
                   "within the same session");
}

ULTRANET_PROBE(kArea, UltraNet_SessionHttpPost) {
    LoopbackServer& server = Loopback();

    UltraNetResponse ignored;
    const UltraNetResult noSession =
        UltraNet_SessionHttpPost(999999, "http://127.0.0.1/", {}, ignored);
    PROBE_EXPECT(!noSession && noSession.code == UltraNetResultCode::InvalidHandle);

    if (!server.Running()) {
        return Implemented("unknown session rejected, but no loopback origin "
                           "for a live session POST: " + server.Error());
    }

    SessionGuard s(UltraNet_CreateSession());
    PROBE_EXPECT(s.handle != UltraNetInvalidHandle);

    UltraNetResponse primed;
    PROBE_EXPECT(static_cast<bool>(
        UltraNet_SessionHttpGet(s.handle, server.Url("/setcookie"), primed)));

    const std::string payload = "session-post-body";
    UltraNetResponse resp;
    const UltraNetResult r = UltraNet_SessionHttpPost(
        s.handle, server.Url("/echo"), std::vector<uint8_t>(payload.begin(), payload.end()),
        resp);
    PROBE_EXPECT_MSG(static_cast<bool>(r), r.message);
    PROBE_EXPECT(resp.headers.Get("x-echo-method") == "POST");
    PROBE_EXPECT(resp.GetBodyAsString() == payload);

    UltraNetResponse cookieCheck;
    PROBE_EXPECT(static_cast<bool>(
        UltraNet_SessionHttpGet(s.handle, server.Url("/cookie"), cookieCheck)));
    PROBE_EXPECT(cookieCheck.GetBodyAsString().find("ultranet_probe") !=
                 std::string::npos);
    return Working("body posted through the session and session cookies "
                   "survived the POST");
}

ULTRANET_PROBE(kArea, UltraNet_SessionLoadCookies) {
    LoopbackServer& server = Loopback();

    const UltraNetResult noSession = UltraNet_SessionLoadCookies(999999, "/tmp/x");
    PROBE_EXPECT(!noSession && noSession.code == UltraNetResultCode::InvalidHandle);

    if (!server.Running()) {
        return Implemented("unknown session rejected; no loopback origin to "
                           "confirm a loaded cookie is actually sent: " +
                           server.Error());
    }

    const std::filesystem::path jar = ScratchDir() / "load_cookies.txt";
    {
        std::ofstream f(jar, std::ios::binary | std::ios::trunc);
        f << "# Netscape HTTP Cookie File\n"
          << "127.0.0.1\tFALSE\t/\tFALSE\t2147483647\tultranet_loaded\tfrom-disk\n";
    }

    SessionGuard s(UltraNet_CreateSession());
    PROBE_EXPECT(s.handle != UltraNetInvalidHandle);
    const UltraNetResult loaded = UltraNet_SessionLoadCookies(s.handle, jar.string());
    PROBE_EXPECT_MSG(static_cast<bool>(loaded), loaded.message);

    UltraNetResponse resp;
    const UltraNetResult r = UltraNet_SessionHttpGet(s.handle, server.Url("/cookie"), resp);
    std::error_code ec;
    std::filesystem::remove(jar, ec);

    PROBE_EXPECT_MSG(static_cast<bool>(r), r.message);
    if (resp.GetBodyAsString().find("ultranet_loaded=from-disk") == std::string::npos) {
        return Broken("cookie file was accepted but the cookie was not sent; "
                      "origin saw \"" + resp.GetBodyAsString() + "\"");
    }
    return Working("cookie read from a Netscape jar on disk and sent on the "
                   "next session request");
}

ULTRANET_PROBE(kArea, UltraNet_SessionSaveCookies) {
    LoopbackServer& server = Loopback();

    const UltraNetResult noSession = UltraNet_SessionSaveCookies(999999, "/tmp/x");
    PROBE_EXPECT(!noSession && noSession.code == UltraNetResultCode::InvalidHandle);

    if (!server.Running()) {
        return Implemented("unknown session rejected; no loopback origin to "
                           "produce a cookie worth saving: " + server.Error());
    }

    const std::filesystem::path jar = ScratchDir() / "save_cookies.txt";
    std::error_code ec;
    std::filesystem::remove(jar, ec);

    SessionGuard s(UltraNet_CreateSession());
    PROBE_EXPECT(s.handle != UltraNetInvalidHandle);

    UltraNetResponse primed;
    PROBE_EXPECT(static_cast<bool>(
        UltraNet_SessionHttpGet(s.handle, server.Url("/setcookie"), primed)));

    const UltraNetResult saved = UltraNet_SessionSaveCookies(s.handle, jar.string());
    PROBE_EXPECT_MSG(static_cast<bool>(saved), saved.message);

    const bool exists = std::filesystem::exists(jar);
    const std::string contents = exists ? ReadWholeFile(jar) : std::string();
    std::filesystem::remove(jar, ec);

    if (!exists) {
        return NotImplemented("returned Ok but wrote no jar file — the "
                              "COOKIELIST=FLUSH is issued on a throwaway easy "
                              "handle whose cookie engine was never started");
    }
    if (contents.find("ultranet_probe") == std::string::npos) {
        return NotImplemented("jar file written but it does not contain the "
                              "session cookie");
    }
    return Working("session cookie flushed to a Netscape jar on disk");
}
