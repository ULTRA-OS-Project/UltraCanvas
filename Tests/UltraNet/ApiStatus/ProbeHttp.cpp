// Tests/UltraNet/ApiStatus/ProbeHttp.cpp
// Probes for UltraNet/UltraNetHttp.h — the header bag, the synchronous verb
// shortcuts, the full request entry point, the async entry point, the file
// helpers, and the request/option features that are part of the same surface
// (streaming chunks, per-request progress, auth, redirects, receive limits).
//
// Everything here runs against the in-process loopback origin, so a machine
// with no internet access still gets verified WORKING entries. Probes degrade
// to IMPLEMENTED (never to WORKING) if the origin cannot be started.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "ApiStatus.h"
#include "ProbeServer.h"

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetHttp.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

using namespace ultranet_apistatus;

namespace {

constexpr const char* kArea = "HTTP";

std::vector<uint8_t> Bytes(const std::string& s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}

// Every network-touching probe funnels through this so "no origin" reads the
// same way everywhere in the report.
Outcome NoOrigin(const LoopbackServer& server, const std::string& what) {
    return Implemented("implementation reached but " + what +
                       " is unverified: loopback origin unavailable (" +
                       server.Error() + ")");
}

std::filesystem::path ScratchDir() {
    std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "ultranet_apistatus";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

} // namespace

// ---------------------------------------------------------------------------
// Header bag
// ---------------------------------------------------------------------------

ULTRANET_PROBE_NAMED(kArea, "UltraNetHttpHeaders (Add/Set/Get/GetAll/Has/Remove)",
                     UltraNetHttpHeaders) {
    UltraNetHttpHeaders h;
    h.Add("Accept", "text/plain");
    h.Add("accept", "application/json");        // duplicates are preserved
    PROBE_EXPECT(h.GetAll("ACCEPT").size() == 2);
    PROBE_EXPECT(h.Get("accept") == "text/plain");   // first wins
    PROBE_EXPECT(h.Has("AcCePt"));

    h.Set("Accept", "*/*");                     // Set replaces every match
    PROBE_EXPECT(h.GetAll("accept").size() == 1);
    PROBE_EXPECT(h.Get("Accept") == "*/*");

    h.Add("X-Trace", "1");
    PROBE_EXPECT(h.Entries().size() == 2);
    PROBE_EXPECT(h.Entries()[0].first == "Accept");   // wire order preserved

    h.Remove("accept");
    PROBE_EXPECT(!h.Has("Accept"));
    PROBE_EXPECT(h.Get("Accept").empty());

    h.Clear();
    PROBE_EXPECT(h.Empty());
    return Working("case-insensitive lookup, duplicate retention, wire order, "
                   "Set-replaces-all, Remove and Clear");
}

// ---------------------------------------------------------------------------
// Synchronous verbs
// ---------------------------------------------------------------------------

ULTRANET_PROBE(kArea, UltraNet_HttpGet) {
    LoopbackServer& server = Loopback();
    if (!server.Running()) return NoOrigin(server, "a live GET");

    UltraNetResponse resp;
    const UltraNetResult r = UltraNet_HttpGet(server.Url("/hello"), resp);
    PROBE_EXPECT_MSG(static_cast<bool>(r), r.message);
    PROBE_EXPECT(resp.statusCode == 200);
    PROBE_EXPECT(resp.IsSuccess());
    PROBE_EXPECT(resp.GetBodyAsString() == "hello from ultranet loopback\n");
    PROBE_EXPECT(resp.contentType.find("text/plain") != std::string::npos);
    PROBE_EXPECT(resp.contentLength == 29);
    PROBE_EXPECT(resp.headers.Has("Content-Type"));
    PROBE_EXPECT(resp.elapsedTime >= 0.0);

    // 4xx must surface as HttpError with the status carried through.
    UltraNetResponse missing;
    const UltraNetResult notFound = UltraNet_HttpGet(server.Url("/status/404"), missing);
    PROBE_EXPECT(!notFound);
    PROBE_EXPECT(notFound.code == UltraNetResultCode::HttpError);
    PROBE_EXPECT(notFound.httpStatus == 404);
    return Working("200 body/headers/content-type/length verified; 404 mapped "
                   "to HttpError with httpStatus preserved");
}

ULTRANET_PROBE(kArea, UltraNet_HttpPost) {
    LoopbackServer& server = Loopback();
    if (!server.Running()) return NoOrigin(server, "a live POST");

    const std::string payload = R"({"probe":"post","n":42})";
    UltraNetResponse resp;
    const UltraNetResult r = UltraNet_HttpPost(server.Url("/echo"), Bytes(payload), resp);
    PROBE_EXPECT_MSG(static_cast<bool>(r), r.message);
    PROBE_EXPECT(resp.statusCode == 200);
    PROBE_EXPECT(resp.headers.Get("x-echo-method") == "POST");
    PROBE_EXPECT(resp.GetBodyAsString() == payload);
    return Working("body round-tripped through the origin; method arrived as POST");
}

ULTRANET_PROBE(kArea, UltraNet_HttpPut) {
    LoopbackServer& server = Loopback();
    if (!server.Running()) return NoOrigin(server, "a live PUT");

    const std::string payload = "put-body-contents";
    UltraNetResponse resp;
    const UltraNetResult r = UltraNet_HttpPut(server.Url("/echo"), Bytes(payload), resp);
    PROBE_EXPECT_MSG(static_cast<bool>(r), r.message);
    PROBE_EXPECT(resp.headers.Get("x-echo-method") == "PUT");
    PROBE_EXPECT(resp.GetBodyAsString() == payload);
    return Working("method arrived as PUT with the body intact");
}

ULTRANET_PROBE(kArea, UltraNet_HttpDelete) {
    LoopbackServer& server = Loopback();
    if (!server.Running()) return NoOrigin(server, "a live DELETE");

    UltraNetResponse resp;
    const UltraNetResult r = UltraNet_HttpDelete(server.Url("/echo"), resp);
    PROBE_EXPECT_MSG(static_cast<bool>(r), r.message);
    PROBE_EXPECT(resp.headers.Get("x-echo-method") == "DELETE");
    return Working("method arrived as DELETE");
}

ULTRANET_PROBE(kArea, UltraNet_HttpHead) {
    LoopbackServer& server = Loopback();
    if (!server.Running()) return NoOrigin(server, "a live HEAD");

    UltraNetResponse resp;
    const UltraNetResult r = UltraNet_HttpHead(server.Url("/hello"), resp);
    PROBE_EXPECT_MSG(static_cast<bool>(r), r.message);
    PROBE_EXPECT(resp.statusCode == 200);
    PROBE_EXPECT_MSG(resp.body.empty(), "HEAD response carried a body");
    PROBE_EXPECT(resp.contentLength == 29);   // advertised, not transferred
    return Working("headers returned with no body; Content-Length still reported");
}

ULTRANET_PROBE(kArea, UltraNet_HttpPatch) {
    LoopbackServer& server = Loopback();
    if (!server.Running()) return NoOrigin(server, "a live PATCH");

    const std::string payload = R"({"op":"replace"})";
    UltraNetResponse resp;
    const UltraNetResult r = UltraNet_HttpPatch(server.Url("/echo"), Bytes(payload), resp);
    PROBE_EXPECT_MSG(static_cast<bool>(r), r.message);
    PROBE_EXPECT(resp.headers.Get("x-echo-method") == "PATCH");
    PROBE_EXPECT(resp.GetBodyAsString() == payload);
    return Working("method arrived as PATCH with the body intact");
}

// ---------------------------------------------------------------------------
// Full-control entry points
// ---------------------------------------------------------------------------

ULTRANET_PROBE(kArea, UltraNet_HttpRequest) {
    LoopbackServer& server = Loopback();

    // The empty-URL rejection needs no origin.
    UltraNetHttpRequest empty;
    UltraNetResponse ignored;
    const UltraNetResult rejected = UltraNet_HttpRequest(empty, ignored);
    PROBE_EXPECT(!rejected && rejected.code == UltraNetResultCode::InvalidUrl);

    if (!server.Running()) return NoOrigin(server, "a live custom request");

    UltraNetHttpRequest req;
    req.url    = server.Url("/echo");
    req.method = UltraNetHttpMethod::Custom;
    req.customMethod = "REPORT";
    req.headers.Set("X-Probe", "custom-verb");
    req.body = Bytes("custom-method-body");

    UltraNetResponse resp;
    const UltraNetResult r = UltraNet_HttpRequest(req, resp);
    PROBE_EXPECT_MSG(static_cast<bool>(r), r.message);
    PROBE_EXPECT(resp.headers.Get("x-echo-method") == "REPORT");
    PROBE_EXPECT(resp.headers.Get("x-echo-probe") == "custom-verb");
    PROBE_EXPECT(resp.GetBodyAsString() == "custom-method-body");
    return Working("custom method, caller headers and body all reached the "
                   "origin; empty URL rejected as InvalidUrl");
}

ULTRANET_PROBE(kArea, UltraNet_HttpRequestAsync) {
    LoopbackServer& server = Loopback();

    // Empty URL must fail fast through the callback, not hang.
    {
        UltraNetHttpRequest bad;
        std::atomic<bool> called{false};
        const UltraNetHandle h = UltraNet_HttpRequestAsync(
            bad, [&](const UltraNetResponse&) { called.store(true); });
        PROBE_EXPECT(h == UltraNetInvalidHandle);
        PROBE_EXPECT(called.load());
    }

    if (!server.Running()) return NoOrigin(server, "a live async request");

    std::mutex m;
    std::condition_variable cv;
    bool done = false;
    UltraNetResponse captured;

    UltraNetHttpRequest req;
    req.url    = server.Url("/echo");
    req.method = UltraNetHttpMethod::Post;
    req.body   = Bytes("async-body");

    const UltraNetHandle h = UltraNet_HttpRequestAsync(
        req, [&](const UltraNetResponse& resp) {
            std::lock_guard<std::mutex> lk(m);
            captured = resp;
            done = true;
            cv.notify_all();
        });
    PROBE_EXPECT(h != UltraNetInvalidHandle);

    std::unique_lock<std::mutex> lk(m);
    const bool fired = cv.wait_for(lk, std::chrono::seconds(10), [&] { return done; });
    PROBE_EXPECT_MSG(fired, "onComplete never fired within 10s");
    PROBE_EXPECT(captured.statusCode == 200);
    PROBE_EXPECT(captured.GetBodyAsString() == "async-body");
    PROBE_EXPECT(captured.headers.Get("x-echo-method") == "POST");
    return Working("async POST completed on the worker thread with the full "
                   "response; empty URL fails through the callback");
}

// ---------------------------------------------------------------------------
// File helpers
// ---------------------------------------------------------------------------

ULTRANET_PROBE(kArea, UltraNet_HttpDownloadFile) {
    LoopbackServer& server = Loopback();

    const UltraNetResult noPath = UltraNet_HttpDownloadFile("http://127.0.0.1/x", "");
    PROBE_EXPECT(!noPath && noPath.code == UltraNetResultCode::InvalidState);

    if (!server.Running()) return NoOrigin(server, "a live download");

    const std::filesystem::path out = ScratchDir() / "download.bin";
    std::error_code ec;
    std::filesystem::remove(out, ec);

    const UltraNetResult r = UltraNet_HttpDownloadFile(
        server.Url("/bytes/65536"), out.string());
    PROBE_EXPECT_MSG(static_cast<bool>(r), r.message);
    PROBE_EXPECT(std::filesystem::exists(out));
    PROBE_EXPECT(std::filesystem::file_size(out) == 65536);

    // Content check: the origin emits a 0x00..0xff ramp.
    std::ifstream in(out, std::ios::binary);
    char probe[4] = {};
    in.read(probe, 4);
    in.close();
    std::filesystem::remove(out, ec);
    PROBE_EXPECT(probe[0] == 0 && probe[1] == 1 && probe[2] == 2 && probe[3] == 3);
    return Working("65536 bytes streamed to disk with byte-exact content; "
                   "empty localPath rejected");
}

ULTRANET_PROBE(kArea, UltraNet_HttpUploadFile) {
    LoopbackServer& server = Loopback();

    UltraNetResponse ignored;
    const UltraNetResult missing = UltraNet_HttpUploadFile(
        "http://127.0.0.1/x", "/nonexistent/ultranet/probe/file", ignored);
    PROBE_EXPECT(!missing && missing.code == UltraNetResultCode::NotFound);

    if (!server.Running()) return NoOrigin(server, "a live upload");

    const std::filesystem::path src = ScratchDir() / "upload.txt";
    const std::string payload(4096, 'u');
    {
        std::ofstream f(src, std::ios::binary | std::ios::trunc);
        f << payload;
    }

    UltraNetResponse resp;
    const UltraNetResult r = UltraNet_HttpUploadFile(
        server.Url("/echo"), src.string(), resp);
    std::error_code ec;
    std::filesystem::remove(src, ec);

    PROBE_EXPECT_MSG(static_cast<bool>(r), r.message);
    PROBE_EXPECT(resp.headers.Get("x-echo-method") == "PUT");
    PROBE_EXPECT_MSG(resp.GetBodyAsString() == payload,
                     "origin echoed " + std::to_string(resp.body.size()) +
                     " bytes, expected " + std::to_string(payload.size()));
    return Working("4096-byte file streamed as a PUT (incl. the 100-continue "
                   "exchange) and echoed back byte-for-byte; missing file rejected");
}

// ---------------------------------------------------------------------------
// Request / option features on the same surface
// ---------------------------------------------------------------------------

ULTRANET_PROBE_NAMED(kArea, "UltraNetHttpRequest::onDataChunk", onDataChunk) {
    LoopbackServer& server = Loopback();
    if (!server.Running()) return NoOrigin(server, "streamed chunk delivery");

    std::size_t total = 0;
    int chunks = 0;
    UltraNetHttpRequest req;
    req.url = server.Url("/bytes/262144");
    req.onDataChunk = [&](const std::vector<uint8_t>& chunk) {
        total += chunk.size();
        ++chunks;
    };

    UltraNetResponse resp;
    const UltraNetResult r = UltraNet_HttpRequest(req, resp);
    PROBE_EXPECT_MSG(static_cast<bool>(r), r.message);
    PROBE_EXPECT(total == 262144);
    PROBE_EXPECT(chunks > 0);
    PROBE_EXPECT_MSG(resp.body.empty(),
                     "response.body must stay empty in streaming mode");
    return Working("262144 bytes delivered in " + std::to_string(chunks) +
                   " chunks; response.body left empty as documented");
}

ULTRANET_PROBE_NAMED(kArea, "UltraNetHttpRequest::onDownloadProgress/onUploadProgress",
                     perRequestProgress) {
    LoopbackServer& server = Loopback();
    if (!server.Running()) return NoOrigin(server, "per-request progress");

    int downloadCalls = 0;
    int64_t lastNow = 0;
    UltraNetHttpRequest req;
    req.url = server.Url("/bytes/524288");
    req.onDownloadProgress = [&](int64_t now, int64_t) {
        ++downloadCalls;
        lastNow = now;
    };
    UltraNetResponse resp;
    const UltraNetResult r = UltraNet_HttpRequest(req, resp);
    PROBE_EXPECT_MSG(static_cast<bool>(r), r.message);
    PROBE_EXPECT_MSG(downloadCalls > 0, "onDownloadProgress never fired");
    PROBE_EXPECT(lastNow > 0);

    int uploadCalls = 0;
    UltraNetHttpRequest up;
    up.url    = server.Url("/echo");
    up.method = UltraNetHttpMethod::Post;
    up.body   = std::vector<uint8_t>(256 * 1024, 'p');
    up.onUploadProgress = [&](int64_t, int64_t) { ++uploadCalls; };
    UltraNetResponse upResp;
    const UltraNetResult ur = UltraNet_HttpRequest(up, upResp);
    PROBE_EXPECT_MSG(static_cast<bool>(ur), ur.message);

    if (uploadCalls == 0) {
        return Implemented("download progress verified (" +
                           std::to_string(downloadCalls) + " callbacks), but "
                           "onUploadProgress did not fire for a 256 KiB POST on "
                           "loopback — libcurl only reports upload progress it "
                           "observes, and loopback sends can complete in one go");
    }
    return Working("download progress fired " + std::to_string(downloadCalls) +
                   " times, upload progress " + std::to_string(uploadCalls) + " times");
}

ULTRANET_PROBE_NAMED(kArea, "UltraNetHttpOptions::followRedirects", followRedirects) {
    LoopbackServer& server = Loopback();
    if (!server.Running()) return NoOrigin(server, "redirect following");

    UltraNetHttpOptions follow;
    follow.followRedirects = true;
    UltraNetResponse followed;
    const UltraNetResult r1 = UltraNet_HttpGet(server.Url("/redirect"), followed, follow);
    PROBE_EXPECT_MSG(static_cast<bool>(r1), r1.message);
    PROBE_EXPECT(followed.statusCode == 200);
    PROBE_EXPECT(followed.GetBodyAsString() == "hello from ultranet loopback\n");
    PROBE_EXPECT(followed.finalUrl.find("/hello") != std::string::npos);

    UltraNetHttpOptions stay;
    stay.followRedirects = false;
    UltraNetResponse notFollowed;
    const UltraNetResult r2 = UltraNet_HttpGet(server.Url("/redirect"), notFollowed, stay);
    PROBE_EXPECT_MSG(static_cast<bool>(r2), r2.message);
    PROBE_EXPECT(notFollowed.statusCode == 302);
    PROBE_EXPECT(notFollowed.headers.Get("Location") == "/hello");
    return Working("302 followed to /hello with finalUrl updated; disabling it "
                   "surfaces the 302 and its Location header");
}

ULTRANET_PROBE_NAMED(kArea, "UltraNetHttpOptions::credentials (Basic/Bearer)", httpAuth) {
    LoopbackServer& server = Loopback();
    if (!server.Running()) return NoOrigin(server, "authentication");

    UltraNetResponse unauth;
    const UltraNetResult challenged = UltraNet_HttpGet(server.Url("/auth"), unauth);
    PROBE_EXPECT(!challenged && challenged.httpStatus == 401);

    UltraNetHttpOptions basic;
    basic.authType = UltraNetAuthType::Basic;
    basic.credentials.type     = UltraNetAuthType::Basic;
    basic.credentials.username = "alice";
    basic.credentials.password = "s3cret";
    UltraNetResponse basicResp;
    const UltraNetResult rb = UltraNet_HttpGet(server.Url("/auth"), basicResp, basic);
    PROBE_EXPECT_MSG(static_cast<bool>(rb), rb.message);
    // "alice:s3cret" base64-encoded.
    PROBE_EXPECT(basicResp.GetBodyAsString() == "auth=Basic YWxpY2U6czNjcmV0");

    UltraNetHttpOptions bearer;
    bearer.authType = UltraNetAuthType::Bearer;
    bearer.credentials.type  = UltraNetAuthType::Bearer;
    bearer.credentials.token = "probe-token-123";
    UltraNetResponse bearerResp;
    const UltraNetResult rt = UltraNet_HttpGet(server.Url("/auth"), bearerResp, bearer);
    PROBE_EXPECT_MSG(static_cast<bool>(rt), rt.message);
    PROBE_EXPECT(bearerResp.GetBodyAsString() == "auth=Bearer probe-token-123");
    return Working("401 challenge answered with a correct Basic header; Bearer "
                   "token sent verbatim");
}

ULTRANET_PROBE_NAMED(kArea, "UltraNetHttpOptions::maxReceiveSize", maxReceiveSize) {
    LoopbackServer& server = Loopback();
    if (!server.Running()) return NoOrigin(server, "the receive limit");

    UltraNetHttpOptions capped;
    capped.maxReceiveSize = 1024;
    UltraNetResponse resp;
    const UltraNetResult r = UltraNet_HttpGet(server.Url("/bytes/65536"), resp, capped);
    PROBE_EXPECT_MSG(!r, "an oversized response was accepted despite maxReceiveSize");
    PROBE_EXPECT(r.code == UltraNetResultCode::ReceiveFailed);
    PROBE_EXPECT(r.message.find("maxReceiveSize") != std::string::npos);

    UltraNetHttpOptions uncapped;
    UltraNetResponse full;
    PROBE_EXPECT(static_cast<bool>(
        UltraNet_HttpGet(server.Url("/bytes/65536"), full, uncapped)));
    PROBE_EXPECT(full.body.size() == 65536);
    return Working("a 64 KiB body is aborted with ReceiveFailed under a 1 KiB "
                   "cap and transfers fully without one");
}

ULTRANET_PROBE_NAMED(kArea, "UltraNetHttpOptions::outputFilePath", outputFilePath) {
    // The field is declared on UltraNetHttpOptions, but the sync/async request
    // paths never read it: only UltraNet_HttpDownloadFile writes to disk, and
    // it takes its own localPath argument.
    LoopbackServer& server = Loopback();
    if (!server.Running()) {
        return NotImplemented("declared on UltraNetHttpOptions but not consulted "
                              "by ConfigureEasyHandle; use "
                              "UltraNet_HttpDownloadFile instead");
    }

    const std::filesystem::path out = ScratchDir() / "outputFilePath.bin";
    std::error_code ec;
    std::filesystem::remove(out, ec);

    UltraNetHttpOptions opt;
    opt.outputFilePath = out.string();
    UltraNetResponse resp;
    const UltraNetResult r = UltraNet_HttpGet(server.Url("/bytes/1024"), resp, opt);
    const bool wroteFile = std::filesystem::exists(out);
    std::filesystem::remove(out, ec);

    PROBE_EXPECT_MSG(static_cast<bool>(r), r.message);
    if (wroteFile) {
        return Working("body streamed to the configured output file");
    }
    return NotImplemented("option ignored: the request succeeded but nothing "
                          "was written to outputFilePath (the body came back in "
                          "response.body instead). Use UltraNet_HttpDownloadFile.");
}

ULTRANET_PROBE_NAMED(kArea, "UltraNetResponse::tlsInfo", responseTlsInfo) {
    // Confirming this positively needs a real TLS handshake, which the
    // plaintext loopback origin cannot provide. Confirming it NEGATIVELY does
    // not: nothing in the core HTTP path ever assigns UltraNetResponse::tlsInfo
    // (ConfigureEasyHandle / FinalizeFromEasy leave it default-constructed), so
    // the field arrives empty on every response, HTTPS or not.
    const std::string url = EnvOverride("ULTRANET_PROBE_HTTPS_URL").empty()
                            ? "https://example.com/"
                            : EnvOverride("ULTRANET_PROBE_HTTPS_URL");

    if (!NetworkProbesEnabled()) {
        return NotImplemented("the field is declared on UltraNetResponse but "
                              "the HTTP path never writes it — TLS session "
                              "details are only available through "
                              "UltraNet_TlsGetInfo on a UltraNet_TlsWrap'd "
                              "socket. Re-run with --network to re-check "
                              "against a live HTTPS peer.");
    }

    UltraNetResponse resp;
    const UltraNetResult r = UltraNet_HttpGet(url, resp);
    if (!r) {
        return Implemented("HTTPS request to " + url + " failed (" + r.message +
                           "), so tlsInfo could not be inspected on a live "
                           "response");
    }
    if (resp.tlsInfo.cipherSuite.empty() &&
        resp.tlsInfo.peerCertificateSubject.empty() &&
        resp.tlsInfo.version == UltraNetTlsVersion::Auto) {
        return NotImplemented("HTTPS request to " + url + " succeeded but "
                              "UltraNetResponse::tlsInfo came back unpopulated");
    }
    return Working("populated from a live HTTPS response against " + url);
}
