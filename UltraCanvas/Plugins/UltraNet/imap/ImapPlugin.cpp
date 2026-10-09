// UltraCanvas/Plugins/UltraNet/imap/ImapPlugin.cpp
// Version: 0.8.0 - one sign-in serves many checks: sessions are kept in a pool
//                  (SessionPool, per server, user and credentials, up to 330 s
//                  idle) and every command that can runs on one (WithSession):
//                  folders, status, envelopes, flags, bodies, a message, flag
//                  changes, expunge and move. Before, each signed in anew - four
//                  sign-ins a check. Bigger blocks: 200 headers to a FETCH, and
//                  bodies cut by size (RFC822.SIZE asked first; 4 MB or 100 to
//                  a FETCH). The URL-based way stays as the fallback.
// Version: 0.7.0 - headers and bodies are fetched in batches - fifty headers or ten
//                  bodies to a FETCH, with BODY.PEEK - on a session of the
//                  plug-in's own (ImapSession, on a connection libcurl signs
//                  in): per message, the URL-based fetch took four to six
//                  round trips, a second a message on a slow link. It stays
//                  as the fallback.
// Version: 0.6.1 - FetchMessages searches with UID SEARCH: it fetched sequence
//                  numbers as UIDs, so it got the wrong mail or none at all
// Version: 0.6.0 - FetchEnvelopesByUid (the envelopes of named messages); a
//                  message whose header could not be read is no longer handed
//                  on as an empty envelope, which the caller stored as a blank
//                  row and never asked for again
// Version: 0.5.0 - reading a message no longer marks it read: libcurl fetches
//                  with BODY[...], which sets \Seen, so an unread message's
//                  \Seen is taken off again (FetchKeepingUnread)
// Version: 0.4.0 - ExpungeMessage (UID EXPUNGE)
// Version: 0.3.0 - AppendMessage sets the flags it is given (found again by
//                  Message-ID, then UID STORE)
// Version: 0.2.0
// IMAP / IMAPS plug-in. Implements the full IMailboxProtocolPlugin surface on
// top of libcurl's native IMAP support: folder listing, mailbox STATUS,
// envelope-only sync, single-message fetch, flag store, move and append — plus
// the original IMailProtocolPlugin::FetchMessages bulk fetch. SendMail is
// delegated to the companion SMTP plug-in. Supports password and XOAUTH2
// (OAuth2 bearer) authentication.
//
// Build: produces libultranet_imap.{so,dylib}. Loaded at runtime by
// UltraNet_RefreshPlugins(). Wire parsing lives in ImapParse.h (pure, tested).
// UltraNet_RefreshPlugins(). Entry point:
//   extern "C" ULTRANET_PLUGIN_EXPORT void UltraNet_PluginInit(const UltraNetPluginHost*);
//
// Strategy:
//   1. Issue a UID SEARCH ALL against the mailbox URL to enumerate UIDs.
//   2. Fetch up to options.maxMessages most-recent messages by UID.
//   3. Parse minimal RFC 822 headers (From / To / Cc / Subject / Date /
//      Content-Type) into UltraNetMailMessage; full body lands in message.body.

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetPlugins.h>
#include "UltraNetPluginHostShim.h"
#include <UltraNet/UltraNetUrl.h>
#include <UltraNet/UltraNetCurlDebug.h>
#include <UltraNet/UltraNetCurlError.h>
#include <UltraNet/UltraNetCurlMailAuth.h>
#include <UltraNet/UltraNetCurlTls.h>

#include "ImapParse.h"

#include <curl/curl.h>

#if defined(_WIN32) || defined(_WIN64)
#include <winsock2.h>   // select() on a session's socket
#else
#include <poll.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using namespace ultranet_imap;

using CurlHandle = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;
CurlHandle NewHandle() { return CurlHandle(curl_easy_init(), curl_easy_cleanup); }

// Percent-encode a mailbox name for use as the path segment of an IMAP URL.
// Folder names are the raw wire names (IMAP modified UTF-7, possibly containing
// spaces, '[', ']', '/', or non-ASCII bytes). libcurl percent-DECODES the URL
// mailbox path before issuing SELECT, so encoding the whole segment (including
// '/'->%2F) round-trips to the exact wire name the server expects. Without this
// such names make libcurl reject the URL with CURLE_URL_MALFORMAT. Only the
// folder segment is encoded; the "/;UID=...;SECTION=..." suffix stays literal.
std::string EncodeMailboxPath(const std::string& folder) {
    return UltraNet_UrlEncode(folder);
}

// Escape a mailbox name for an IMAP quoted-string ("...") inside a command.
// RFC 3501 quoted-strings escape only '\' and '"' — this is NOT URL-encoding,
// because these go on the wire as literal IMAP text, not as a URL.
std::string QuoteImapMailbox(const std::string& folder) {
    std::string out;
    out.reserve(folder.size() + 2);
    for (char c : folder) {
        if (c == '\\' || c == '"') out += '\\';
        out += c;
    }
    return out;
}

std::size_t WriteToString(char* data, std::size_t size, std::size_t nmemb, void* ud) {
    static_cast<std::string*>(ud)->append(data, size * nmemb);
    return size * nmemb;
}

struct ReadCtx { const std::string* data; std::size_t offset; };
std::size_t ReadFromString(char* buffer, std::size_t size, std::size_t nitems, void* ud) {
    auto* ctx = static_cast<ReadCtx*>(ud);
    std::size_t remaining = ctx->data->size() - ctx->offset;
    std::size_t n = std::min(size * nitems, remaining);
    if (n) { std::memcpy(buffer, ctx->data->data() + ctx->offset, n); ctx->offset += n; }
    return n;
}

UltraNetResultCode MapCurlError(CURLcode rc) {
    switch (rc) {
        case CURLE_OK:                       return UltraNetResultCode::Success;
        case CURLE_URL_MALFORMAT:            return UltraNetResultCode::InvalidUrl;
        case CURLE_COULDNT_RESOLVE_HOST:     return UltraNetResultCode::HostNotFound;
        case CURLE_COULDNT_CONNECT:          return UltraNetResultCode::ConnectionRefused;
        case CURLE_OPERATION_TIMEDOUT:       return UltraNetResultCode::Timeout;
        case CURLE_LOGIN_DENIED:             return UltraNetResultCode::AuthenticationFailed;
        case CURLE_SSL_CONNECT_ERROR:        return UltraNetResultCode::TlsHandshakeFailed;
        case CURLE_PEER_FAILED_VERIFICATION: return UltraNetResultCode::TlsCertificateInvalid;
        case CURLE_SSL_CACERT_BADFILE:       return UltraNetResultCode::TlsCertificateInvalid;
        case CURLE_SEND_ERROR:               return UltraNetResultCode::SendFailed;
        case CURLE_RECV_ERROR:               return UltraNetResultCode::ReceiveFailed;
        default:                             return UltraNetResultCode::Unknown;
    }
}

// Rebuild "imap(s)://[user[:pass]@]host[:port]/" from a server URL.
bool ParseServerBase(const std::string& serverUrl, std::string& outBase, bool& outTls) {
    UltraNetUrlComponents c;
    if (!UltraNet_ParseUrl(serverUrl, c)) return false;
    if (c.scheme != "imap" && c.scheme != "imaps") return false;
    outTls = (c.scheme == "imaps");
    std::ostringstream os;
    os << c.scheme << "://";
    if (!c.username.empty()) {
        os << c.username;
        if (!c.password.empty()) os << ':' << c.password;
        os << '@';
    }
    os << c.host;
    if (c.port > 0) os << ':' << c.port;
    os << '/';
    outBase = os.str();
    return true;
}

constexpr const char* kPluginVersion = "0.2.0";

UltraNetResult ApplyCommonOptions(CURL* h, const UltraNetMailOptions& opt, bool implicitTls) {
    ultranet_curldebug::EnableIfRequested(h);
    // STARTTLS here is CURLUSESSL_TRY (below): an upgrade if the server offers one.
    ultranet_curlmailauth::RecordContext(std::string("UltraNet IMAP plug-in ") + kPluginVersion,
                                         opt, ultranet_curlmailauth::Protocol::Imap,
                                         implicitTls, /*startTlsRequired=*/false);
    if (UltraNetResult a = ultranet_curlmailauth::Apply(
            h, opt, ultranet_curlmailauth::Protocol::Imap); !a)
        return a;
    if (opt.useTls || implicitTls) {
        curl_easy_setopt(h, CURLOPT_USE_SSL,
                         implicitTls ? CURLUSESSL_ALL : CURLUSESSL_TRY);
        // Trust the same CA anchors as the HTTP client. Matters on Windows,
        // where the system libcurl's baked-in CA path does not exist on an end
        // user's machine; empty leaves libcurl's own default in place.
        const std::string caBundle = UltraNet_ResolveCaBundlePath();
        if (!caBundle.empty())
            curl_easy_setopt(h, CURLOPT_CAINFO, caBundle.c_str());
        // Windows: system certificate store + browser-style revocation check.
        ultranet_curltls::Apply(h);
    }
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(opt.connectTimeoutMs));
    curl_easy_setopt(h, CURLOPT_TIMEOUT_MS,        static_cast<long>(opt.operationTimeoutMs));
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
    return UltraNetResult::Ok();
}

// One IMAP transfer on an EXISTING, already-configured handle (ApplyCommonOptions
// done once by the caller). This is what makes connection reuse possible: repeated
// calls on the same handle keep curl's authenticated connection alive instead of
// reconnecting (TCP + TLS + XOAUTH2) per message. `customReq` empty = a GET-style
// body/section fetch; otherwise a UID command (SEARCH / FETCH / STORE / ...).
UltraNetResult PerformOn(CURL* h, const std::string& url,
                         const std::string& customReq, std::string& outBody) {
    outBody.clear();
    curl_easy_setopt(h, CURLOPT_URL, url.c_str());
    // Reset the custom request each call: a leftover CUSTOMREQUEST would turn a
    // plain body fetch into the previous command.
    curl_easy_setopt(h, CURLOPT_CUSTOMREQUEST, customReq.empty() ? nullptr : customReq.c_str());
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, &WriteToString);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &outBody);
    std::string why, diagnostics;
    CURLcode rc = ultranet_curlerror::Perform(h, why, &diagnostics);
    if (rc != CURLE_OK)
        return ultranet_curlerror::Error(MapCurlError(rc), why, diagnostics);
    return UltraNetResult::Ok();
}

// Run a custom IMAP command against `url`, capturing the untagged response. One
// standalone connection — for the one-off operations (LIST, STATUS, STORE, MOVE).
UltraNetResult RunCommand(const std::string& url, const std::string& customReq,
                          const UltraNetMailOptions& opt, bool implicitTls,
                          std::string& outBody) {
    CurlHandle h = NewHandle();
    if (!h)
        return UltraNetResult::Error(UltraNetResultCode::InsufficientMemory, "curl_easy_init failed");
    if (UltraNetResult a = ApplyCommonOptions(h.get(), opt, implicitTls); !a) return a;
    return PerformOn(h.get(), url, customReq, outBody);
}

// A conversation in commands of the plug-in's own, on a connection libcurl
// opened and signed in (CURLOPT_CONNECT_ONLY: TLS or STARTTLS, the password or
// XOAUTH2, exactly as for every other call here). libcurl's IMAP takes one
// message per URL, fetches with BODY[] (which marks it read), and for a command
// of the caller's own hands on only the lines that begin with '*' - a message's
// text, which comes as a literal, is lost. Read here whole, literals and all,
// one FETCH carries the headers of hundreds of messages, and BODY.PEEK leaves
// them unread without their flags being read before and restored after each
// fetch. A session outlives the call that opened it (SessionPool): every call
// of a mail check, and the next check, use the one sign-in.
class ImapSession {
public:
    // Connect to `base` and sign in. `curlCode` (optional) receives libcurl's
    // own code, so a caller can tell a server it cannot reach from a session
    // libcurl would not open.
    UltraNetResult Open(const std::string& base, const UltraNetMailOptions& opt, bool tls,
                        CURLcode* curlCode = nullptr) {
        h_ = NewHandle();
        if (!h_)
            return UltraNetResult::Error(UltraNetResultCode::InsufficientMemory,
                                         "curl_easy_init failed");
        if (UltraNetResult a = ApplyCommonOptions(h_.get(), opt, tls); !a) return a;
        curl_easy_setopt(h_.get(), CURLOPT_URL, base.c_str());
        curl_easy_setopt(h_.get(), CURLOPT_CONNECT_ONLY, 1L);
        SetTimeout(opt.operationTimeoutMs);
        std::string why, diagnostics;
        const CURLcode rc = ultranet_curlerror::Perform(h_.get(), why, &diagnostics);
        if (curlCode) *curlCode = rc;
        if (rc != CURLE_OK) {
            h_.reset();
            return ultranet_curlerror::Error(MapCurlError(rc), why, diagnostics);
        }
        Touch();
        return UltraNetResult::Ok();
    }

    // How long a command may wait for the server's next bytes.
    void SetTimeout(int operationTimeoutMs) {
        idleMs_ = operationTimeoutMs > 0 ? operationTimeoutMs : 60000;
    }

    // Send `command` and read up to its tagged completion. Each untagged
    // response on the way goes to `onUntagged` as soon as it is complete. Ok
    // when the server answered OK, its own words when it did not.
    UltraNetResult Run(const std::string& command,
                       const std::function<void(const ImapResponse&)>& onUntagged = {}) {
        if (!h_ || broken_)
            return UltraNetResult::Error(UltraNetResultCode::InvalidState,
                                         "the IMAP session is not open");
        const std::string tag = "U" + std::to_string(++tag_);
        if (UltraNetResult s = SendAll(tag + " " + command + "\r\n"); !s) return s;
        ImapResponse r;
        for (;;) {
            while (reader_.Next(r)) {
                if (r.IsTagged(tag)) {
                    Touch();
                    if (r.Status() == "OK") return UltraNetResult::Ok();
                    const std::string verb = command.substr(0, command.find(' ', 4));
                    return UltraNetResult::Error(UltraNetResultCode::Unknown,
                        "The mail server refused " + verb + ": " + r.Text());
                }
                if (!r.segments.empty() && r.segments.front().compare(0, 6, "* BYE ") == 0)
                    broken_ = true;   // the server is closing the connection
                if (onUntagged && !r.segments.empty() &&
                    r.segments.front().compare(0, 2, "* ") == 0)
                    onUntagged(r);
            }
            if (UltraNetResult more = ReadMore(); !more) return more;
        }
    }

    // Open `folder` read-only - a fresh view of it, so it is issued every time.
    UltraNetResult Examine(const std::string& folder) { return Choose("EXAMINE", folder, false); }
    // Open `folder` read-write, for a change to its messages.
    UltraNetResult Select(const std::string& folder) {
        if (selected_ == folder && readWrite_) return UltraNetResult::Ok();
        return Choose("SELECT", folder, true);
    }
    // Leave the open folder without expunging anything - which CLOSE would do
    // to a folder opened read-write: UNSELECT where the server has it, else
    // an EXAMINE that fails, which leaves no folder open (RFC 3501 6.3.1).
    void Deselect() {
        if (selected_.empty()) return;
        if (!Run("UNSELECT")) Run("EXAMINE \"\"");
        selected_.clear();
        readWrite_ = false;
    }
    const std::string& Selected() const { return selected_; }

    // Whether the connection still answers, at most `limitMs` to find out: a
    // kept connection the server or a router has dropped since must not hold
    // up a check for the whole operation timeout.
    bool StillAnswers(long limitMs) {
        const long saved = idleMs_;
        idleMs_ = limitMs;
        const bool ok = static_cast<bool>(Run("NOOP"));
        idleMs_ = saved;
        return ok && !broken_;
    }

    // A connection that failed (it closed, timed out, or said BYE) is not used
    // again.
    bool Broken() const { return broken_ || !h_; }
    long long IdleMs() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - lastUsed_).count();
    }

private:
    UltraNetResult Choose(const char* verb, const std::string& folder, bool readWrite) {
        UltraNetResult r = Run(std::string(verb) + " \"" + QuoteImapMailbox(folder) + "\"");
        if (r) { selected_ = folder; readWrite_ = readWrite; }
        else   { selected_.clear(); readWrite_ = false; }   // a failed SELECT leaves none open
        return r;
    }

    UltraNetResult SendAll(const std::string& data) {
        std::size_t done = 0;
        while (done < data.size()) {
            std::size_t n = 0;
            const CURLcode rc = curl_easy_send(h_.get(), data.data() + done,
                                               data.size() - done, &n);
            if (rc == CURLE_OK) { done += n; continue; }
            if (rc != CURLE_AGAIN) return Failed(rc);
            if (!WaitForSocket(/*forReading=*/false)) return TimedOut();
        }
        return UltraNetResult::Ok();
    }

    UltraNetResult ReadMore() {
        char buf[64 * 1024];
        for (;;) {
            std::size_t n = 0;
            const CURLcode rc = curl_easy_recv(h_.get(), buf, sizeof buf, &n);
            if (rc == CURLE_OK) {
                if (n == 0) {
                    broken_ = true;
                    return UltraNetResult::Error(UltraNetResultCode::ReceiveFailed,
                                                 "The mail server closed the connection.");
                }
                reader_.Feed(buf, n);
                return UltraNetResult::Ok();
            }
            if (rc != CURLE_AGAIN) return Failed(rc);
            if (!WaitForSocket(/*forReading=*/true)) return TimedOut();
        }
    }

    // Wait until the connection can be read (or written), at most idleMs_.
    bool WaitForSocket(bool forReading) {
        curl_socket_t sock = CURL_SOCKET_BAD;
        if (curl_easy_getinfo(h_.get(), CURLINFO_ACTIVESOCKET, &sock) != CURLE_OK ||
            sock == CURL_SOCKET_BAD)
            return false;
#if defined(_WIN32) || defined(_WIN64)
        fd_set set;
        FD_ZERO(&set);
        FD_SET(sock, &set);
        timeval tv;
        tv.tv_sec  = static_cast<long>(idleMs_ / 1000);
        tv.tv_usec = static_cast<long>((idleMs_ % 1000) * 1000);
        return select(0, forReading ? &set : nullptr, forReading ? nullptr : &set,
                      nullptr, &tv) > 0;
#else
        pollfd pfd{};
        pfd.fd = sock;
        pfd.events = forReading ? POLLIN : POLLOUT;
        return poll(&pfd, 1, static_cast<int>(idleMs_)) > 0;
#endif
    }

    UltraNetResult Failed(CURLcode rc) {
        broken_ = true;
        return UltraNetResult::Error(MapCurlError(rc), curl_easy_strerror(rc));
    }
    UltraNetResult TimedOut() {
        broken_ = true;   // a response may still be on its way: the stream is out of step
        return UltraNetResult::Error(UltraNetResultCode::Timeout,
            "The mail server did not answer for " + std::to_string(idleMs_ / 1000) +
            " seconds.");
    }
    void Touch() { lastUsed_ = std::chrono::steady_clock::now(); }

    CurlHandle          h_{nullptr, curl_easy_cleanup};
    ImapResponseReader  reader_;
    int                 tag_ = 0;
    long                idleMs_ = 60000;
    bool                broken_ = false;
    std::string         selected_;
    bool                readWrite_ = false;
    std::chrono::steady_clock::time_point lastUsed_ = std::chrono::steady_clock::now();
};

// Signed-in sessions kept for the next call. A mail check is several calls -
// the folder list, the inbox's status, its new mail, its flags, its bodies -
// and with a short check interval one follows the other every 20 seconds;
// each signing in anew was four sign-ins a check, which some providers limit.
// A session is kept for the server, user and credentials that opened it, and
// for as long as a check every five minutes still finds it; one the server or
// a router has dropped in the meantime is noticed by a NOOP before reuse.
class SessionPool {
public:
    // A kept session for `key`, or none.
    std::unique_ptr<ImapSession> Take(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        Prune();
        for (auto it = idle_.begin(); it != idle_.end(); ++it) {
            if (it->first != key) continue;
            std::unique_ptr<ImapSession> s = std::move(it->second);
            idle_.erase(it);
            return s;
        }
        return nullptr;
    }
    // Keep `session` for the next call - unless its connection broke.
    void Give(const std::string& key, std::unique_ptr<ImapSession> session) {
        if (!session || session->Broken()) return;
        std::lock_guard<std::mutex> lock(mutex_);
        Prune();
        std::size_t forKey = 0;
        for (const auto& e : idle_) if (e.first == key) ++forKey;
        if (forKey >= kPerKey || idle_.size() >= kTotal) return;   // closed instead
        idle_.emplace_back(key, std::move(session));
    }
    void Clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        idle_.clear();
    }

private:
    // Two per account (a check and a folder refresh may overlap), a handful in all.
    static constexpr std::size_t kPerKey = 2;
    static constexpr std::size_t kTotal  = 16;
    // Long enough for a check every five minutes to find its session again.
    static constexpr long long kMaxIdleMs = 330000;

    void Prune() {
        idle_.erase(std::remove_if(idle_.begin(), idle_.end(), [](const auto& e) {
            return !e.second || e.second->Broken() || e.second->IdleMs() > kMaxIdleMs;
        }), idle_.end());
    }

    std::mutex mutex_;
    std::vector<std::pair<std::string, std::unique_ptr<ImapSession>>> idle_;
};

// Never destroyed: a session's connection belongs to libcurl, and at process
// exit libcurl may already be cleaned up (Shutdown() empties the pool first).
SessionPool& Pool() {
    static SessionPool* pool = new SessionPool;
    return *pool;
}

// What a kept session must match to be reused: the server, the user and every
// setting its sign-in used. The password or token stays in memory, as it does
// in the libcurl handle of the session itself.
std::string SessionKey(const std::string& base, bool tls, const UltraNetMailOptions& o) {
    std::ostringstream k;
    k << base << '\n' << tls << o.useTls << o.implicitTls << '\n'
      << static_cast<int>(o.auth) << ':' << static_cast<int>(o.credentials.type) << '\n'
      << o.credentials.username << '\n' << o.credentials.password << '\n'
      << o.credentials.token << '\n' << o.credentials.realm << '\n' << o.connectTimeoutMs;
    for (const auto& [name, value] : o.credentials.custom) k << '\n' << name << '=' << value;
    return k.str();
}

// Whether a session that would not open is worth the old, per-message way:
// not when the server cannot be reached or refuses the sign-in - that fails
// the same way again - but when libcurl would not open a session as such.
bool SessionUnavailable(CURLcode rc) {
    switch (rc) {
        case CURLE_COULDNT_RESOLVE_HOST:
        case CURLE_COULDNT_RESOLVE_PROXY:
        case CURLE_COULDNT_CONNECT:
        case CURLE_OPERATION_TIMEDOUT:
        case CURLE_LOGIN_DENIED:
        case CURLE_SSL_CONNECT_ERROR:
        case CURLE_PEER_FAILED_VERIFICATION:
        case CURLE_SSL_CACERT_BADFILE:
        case CURLE_SEND_ERROR:
        case CURLE_RECV_ERROR:
        case CURLE_USE_SSL_FAILED:
            return false;
        default:
            return true;
    }
}

// Parse a full RFC 822 message into UltraNetMailMessage (for bulk FetchMessages).
void ParseFullMessage(const std::string& raw, UltraNetMailMessage& m) {
    std::size_t headerEnd = raw.find("\r\n\r\n");
    std::size_t sepLen = 4;
    if (headerEnd == std::string::npos) { headerEnd = raw.find("\n\n"); sepLen = 2; }
    const std::string headerBlock = (headerEnd == std::string::npos) ? raw : raw.substr(0, headerEnd);

    UltraNetMailEnvelope env;
    ParseEnvelopeHeaders(headerBlock, env);
    m.from = env.from;
    m.to = env.to;
    m.cc = env.cc;
    m.subject = env.subject;
    if (headerEnd != std::string::npos && headerEnd + sepLen < raw.size())
        m.body = raw.substr(headerEnd + sepLen);
}

// ============================================================================
// ImapPlugin
// ============================================================================
class ImapPlugin : public IMailboxProtocolPlugin {
public:
    std::string GetName() const override    { return "UltraNet-IMAP"; }
    std::string GetVersion() const override { return kPluginVersion; }
    std::vector<std::string> GetSupportedSchemes() const override {
        return {"imap", "imaps"};
    }
    UltraNetResult Initialize(const UltraNetConfig&) override { return UltraNetResult::Ok(); }
    void Shutdown() override { Pool().Clear(); }

    UltraNetResult SendMail(const UltraNetMailMessage&,
                            const UltraNetMailOptions&) override {
        return UltraNetResult::Error(UltraNetResultCode::UnsupportedScheme,
            "IMAP plug-in does not send; use the UltraNet-SMTP plug-in");
    }

    // ---- Original bulk fetch (kept for compatibility) ----------------------
    UltraNetResult FetchMessages(const std::string& url,
                                 std::vector<UltraNetMailMessage>& outMessages,
                                 const UltraNetMailOptions& options) override {
        outMessages.clear();
        std::string base, mailbox;
        bool tls = false;
        if (!SplitMailboxUrl(url, base, mailbox, tls))
            return UltraNetResult::Error(UltraNetResultCode::InvalidUrl,
                "expected imap:// or imaps:// URL with a mailbox path");

        // `mailbox` here is the path segment already parsed out of the caller's
        // URL by SplitMailboxUrl, so it is URL-derived — do NOT EncodeMailboxPath
        // it again (that would double-encode). The IMailboxProtocolPlugin methods
        // above take a raw folder name and encode it themselves.
        // UID SEARCH, not SEARCH: the messages are fetched by UID below
        // (";UID=n"), and a plain SEARCH answers with sequence numbers, which
        // stop matching the UIDs as soon as a message has ever been deleted.
        std::string searchBody;
        UltraNetResult sr = RunCommand(base + mailbox, "UID SEARCH ALL", options, tls, searchBody);
        if (!sr) return sr;
        std::vector<uint32_t> uids = ParseSearchUids(searchBody);
        if (uids.empty()) return UltraNetResult::Ok();

        std::size_t take = uids.size();
        if (options.maxMessages > 0 && static_cast<std::size_t>(options.maxMessages) < take)
            take = static_cast<std::size_t>(options.maxMessages);
        CurlHandle h = NewHandle();
        if (!h)
            return UltraNetResult::Error(UltraNetResultCode::InsufficientMemory, "curl_easy_init failed");
        if (UltraNetResult a = ApplyCommonOptions(h.get(), options, tls); !a) return a;
        for (std::size_t i = uids.size() - take; i < uids.size(); ++i) {
            std::string raw;
            std::ostringstream u; u << base << mailbox << "/;UID=" << uids[i];
            if (!FetchKeepingUnread(h.get(), base + mailbox, u.str(), uids[i], raw, nullptr)
                || raw.empty()) continue;
            UltraNetMailMessage msg;
            ParseFullMessage(raw, msg);
            outMessages.push_back(std::move(msg));
        }
        return UltraNetResult::Ok();
    }

    // ---- IMailboxProtocolPlugin --------------------------------------------
    UltraNetResult ListFolders(const std::string& serverUrl,
                               std::vector<UltraNetMailFolder>& outFolders,
                               const UltraNetMailOptions& options) override {
        outFolders.clear();
        std::string base; bool tls = false;
        if (!ParseServerBase(serverUrl, base, tls))
            return UltraNetResult::Error(UltraNetResultCode::InvalidUrl, "bad imap server URL");
        bool unavailable = false;
        UltraNetResult listed = WithSession(base, tls, options, [&](ImapSession& s) {
            std::string text;
            UltraNetResult lr = s.Run("LIST \"\" \"*\"", [&](const ImapResponse& r) {
                text += r.AsLine() + "\r\n";
            });
            if (lr) outFolders = ParseListResponse(text);
            return lr;
        }, &unavailable);
        if (listed || !unavailable) return listed;
        std::string body;
        UltraNetResult r = RunCommand(base, "LIST \"\" \"*\"", options, tls, body);
        if (!r) return r;
        outFolders = ParseListResponse(body);
        return UltraNetResult::Ok();
    }

    UltraNetResult GetMailboxStatus(const std::string& serverUrl,
                                    const std::string& folder,
                                    UltraNetMailboxStatus& outStatus,
                                    const UltraNetMailOptions& options) override {
        std::string base; bool tls = false;
        if (!ParseServerBase(serverUrl, base, tls))
            return UltraNetResult::Error(UltraNetResultCode::InvalidUrl, "bad imap server URL");
        std::string cmd = "STATUS \"" + QuoteImapMailbox(folder) +
            "\" (MESSAGES RECENT UIDNEXT UIDVALIDITY UNSEEN)";
        bool unavailable = false;
        UltraNetResult status = WithSession(base, tls, options, [&](ImapSession& s) {
            // STATUS of the folder a session has open may be out of date (RFC
            // 3501 6.3.10) - and a stale UIDNEXT would look like a mailbox
            // that was renumbered.
            if (s.Selected() == folder) s.Deselect();
            std::string text;
            UltraNetResult sr = s.Run(cmd, [&](const ImapResponse& r) {
                text += r.AsLine() + "\r\n";
            });
            if (sr) outStatus = ParseStatusResponse(text);
            return sr;
        }, &unavailable);
        if (status || !unavailable) return status;
        std::string body;
        UltraNetResult r = RunCommand(base, cmd, options, tls, body);
        if (!r) return r;
        outStatus = ParseStatusResponse(body);
        return UltraNetResult::Ok();
    }

    // Batch form: collect the streamed envelopes into the caller's vector.
    UltraNetResult FetchEnvelopes(const std::string& serverUrl,
                                  const std::string& folder,
                                  uint32_t sinceUid,
                                  std::vector<UltraNetMailEnvelope>& outEnvelopes,
                                  const UltraNetMailOptions& options) override {
        outEnvelopes.clear();
        return FetchEnvelopes(serverUrl, folder, sinceUid,
                              [&outEnvelopes](const UltraNetMailEnvelope& e) {
                                  outEnvelopes.push_back(e);
                              },
                              options);
    }

    // Streaming form: fire `onEnvelope` for each message as its header/flags land
    // over the one reused connection, so the UI can fill the list incrementally.
    UltraNetResult FetchEnvelopes(const std::string& serverUrl,
                                  const std::string& folder,
                                  uint32_t sinceUid,
                                  const std::function<void(const UltraNetMailEnvelope&)>& onEnvelope,
                                  const UltraNetMailOptions& options) override {
        std::string base; bool tls = false;
        if (!ParseServerBase(serverUrl, base, tls))
            return UltraNetResult::Error(UltraNetResultCode::InvalidUrl, "bad imap server URL");
        std::ostringstream search;
        if (sinceUid > 0) search << "UID SEARCH UID " << (sinceUid + 1) << ":*";
        else              search << "UID SEARCH ALL";
        // Which messages, newest first, bounded by maxMessages.
        auto choose = [&](std::vector<uint32_t> uids) {
            // "UID n:*" always matches the highest UID, even one below n (RFC
            // 3501 6.4.8): that message is held already.
            if (sinceUid > 0)
                uids.erase(std::remove_if(uids.begin(), uids.end(),
                                          [sinceUid](uint32_t u) { return u <= sinceUid; }),
                           uids.end());
            std::sort(uids.begin(), uids.end(), std::greater<uint32_t>());
            if (options.maxMessages > 0 &&
                static_cast<std::size_t>(options.maxMessages) < uids.size())
                uids.resize(static_cast<std::size_t>(options.maxMessages));
            return uids;
        };

        // Batched: one SEARCH, then the headers in large FETCHes, on the
        // account's kept session.
        {
            std::vector<uint32_t> rest;
            bool unavailable = false;
            UltraNetResult batched = WithSession(base, tls, options, [&](ImapSession& s) {
                if (UltraNetResult ex = s.Examine(folder); !ex) return ex;
                std::string searchText;
                UltraNetResult sr = s.Run(search.str(), [&](const ImapResponse& r) {
                    searchText += r.Text() + "\r\n";
                });
                if (!sr) return sr;
                rest = FetchHeadersBatched(s, choose(ParseSearchUids(searchText)), onEnvelope);
                return UltraNetResult::Ok();
            }, &unavailable);
            if (batched) {
                if (rest.empty()) return UltraNetResult::Ok();
                // What the batches did not bring (a FETCH refused, a header
                // not sent as a literal): one by one.
                CurlHandle h = NewHandle();
                if (!h || !ApplyCommonOptions(h.get(), options, tls)) return UltraNetResult::Ok();
                FetchHeaders(h.get(), base, folder, rest, onEnvelope);
                return UltraNetResult::Ok();
            }
            if (!unavailable) return batched;
        }

        // Per message: when libcurl would not open a session of the plug-in's own.
        const std::string mbUrl = base + EncodeMailboxPath(folder);

        // One handle for the whole pass: the SEARCH and every per-UID header/flags
        // fetch reuse the same authenticated connection instead of reconnecting.
        CurlHandle h = NewHandle();
        if (!h)
            return UltraNetResult::Error(UltraNetResultCode::InsufficientMemory, "curl_easy_init failed");
        if (UltraNetResult a = ApplyCommonOptions(h.get(), options, tls); !a) return a;
        std::string searchBody;
        UltraNetResult sr = PerformOn(h.get(), mbUrl, search.str(), searchBody);
        if (!sr) return sr;
        FetchHeaders(h.get(), base, folder, choose(ParseSearchUids(searchBody)), onEnvelope);
        return UltraNetResult::Ok();
    }

    UltraNetResult FetchEnvelopesByUid(const std::string& serverUrl,
                                       const std::string& folder,
                                       const std::vector<uint32_t>& uids,
                                       const std::function<void(const UltraNetMailEnvelope&)>& onEnvelope,
                                       const UltraNetMailOptions& options) override {
        if (uids.empty()) return UltraNetResult::Ok();
        std::string base; bool tls = false;
        if (!ParseServerBase(serverUrl, base, tls))
            return UltraNetResult::Error(UltraNetResultCode::InvalidUrl, "bad imap server URL");

        std::vector<uint32_t> newestFirst(uids);
        std::sort(newestFirst.begin(), newestFirst.end(), std::greater<uint32_t>());
        newestFirst.erase(std::unique(newestFirst.begin(), newestFirst.end()), newestFirst.end());

        std::vector<uint32_t> rest = newestFirst;
        {
            bool unavailable = false;
            UltraNetResult batched = WithSession(base, tls, options, [&](ImapSession& s) {
                if (UltraNetResult ex = s.Examine(folder); !ex) return ex;
                rest = FetchHeadersBatched(s, newestFirst, onEnvelope);
                return UltraNetResult::Ok();
            }, &unavailable);
            if (batched && rest.empty()) return UltraNetResult::Ok();
            if (!batched && !unavailable) return batched;
        }

        CurlHandle h = NewHandle();
        if (!h)
            return UltraNetResult::Error(UltraNetResultCode::InsufficientMemory, "curl_easy_init failed");
        if (UltraNetResult a = ApplyCommonOptions(h.get(), options, tls); !a) return a;
        // The mailbox is selected by the first fetch; a connection that fails
        // already there fails the call, so the caller can tell it from a
        // message that is simply gone.
        const std::string mbUrl = base + EncodeMailboxPath(folder);
        std::string probe;
        if (UltraNetResult r = PerformOn(h.get(), mbUrl, "NOOP", probe); !r) return r;
        FetchHeaders(h.get(), base, folder, rest, onEnvelope);
        return UltraNetResult::Ok();
    }

    UltraNetResult FetchMessage(const std::string& serverUrl,
                                const std::string& folder,
                                uint32_t uid,
                                std::string& outRaw,
                                const UltraNetMailOptions& options) override {
        outRaw.clear();
        std::string base; bool tls = false;
        if (!ParseServerBase(serverUrl, base, tls))
            return UltraNetResult::Error(UltraNetResultCode::InvalidUrl, "bad imap server URL");
        // On the account's kept session - a message opened in the reading pane
        // no longer signs in to fetch it.
        bool unavailable = false, got = false;
        UltraNetResult fetched = WithSession(base, tls, options, [&](ImapSession& s) {
            if (UltraNetResult ex = s.Examine(folder); !ex) return ex;
            return s.Run("UID FETCH " + std::to_string(uid) + " (UID BODY.PEEK[])",
                         [&](const ImapResponse& r) {
                ImapFetchItem item;
                if (!ParseFetchResponse(r, item) || item.uid != uid) return;
                const auto body = item.sections.find("BODY[]");
                if (body == item.sections.end()) return;
                outRaw = body->second;
                got = true;
            });
        }, &unavailable);
        if (fetched && got) return UltraNetResult::Ok();
        if (!fetched && !unavailable) return fetched;
        // Not sent as a literal, or no session: the per-message way.
        const std::string mbUrl = base + EncodeMailboxPath(folder);
        std::ostringstream u; u << mbUrl << "/;UID=" << uid;
        CurlHandle h = NewHandle();
        if (!h)
            return UltraNetResult::Error(UltraNetResultCode::InsufficientMemory, "curl_easy_init failed");
        if (UltraNetResult a = ApplyCommonOptions(h.get(), options, tls); !a) return a;
        if (!FetchKeepingUnread(h.get(), mbUrl, u.str(), uid, outRaw, nullptr))
            return UltraNetResult::Error(UltraNetResultCode::Unknown, "fetch failed");
        return UltraNetResult::Ok();
    }

    // Fetch many bodies over ONE authenticated connection: the whole point of the
    // performance fix. Without this, the sync engine calls FetchMessage per
    // message and reconnects (TCP + TLS + XOAUTH2) each time — minutes for a
    // mailbox that should take seconds. Bodies are best-effort: a UID whose fetch
    // fails is skipped, not fatal (matches the previous per-message behaviour).
    UltraNetResult FetchMessageBodies(
        const std::string& serverUrl, const std::string& folder,
        const std::vector<uint32_t>& uids,
        const std::function<void(uint32_t uid, const std::string& raw)>& onMessage,
        const UltraNetMailOptions& options) override {
        if (uids.empty()) return UltraNetResult::Ok();
        std::string base; bool tls = false;
        if (!ParseServerBase(serverUrl, base, tls))
            return UltraNetResult::Error(UltraNetResultCode::InvalidUrl, "bad imap server URL");

        // Batched: bodies in FETCHes of a few megabytes, BODY.PEEK so they
        // stay unread, on the account's kept session.
        std::vector<uint32_t> rest = uids;
        {
            bool unavailable = false;
            UltraNetResult batched = WithSession(base, tls, options, [&](ImapSession& s) {
                if (UltraNetResult ex = s.Examine(folder); !ex) return ex;
                rest = FetchBodiesBatched(s, uids, onMessage);
                return UltraNetResult::Ok();
            }, &unavailable);
            if (batched && rest.empty()) return UltraNetResult::Ok();
            if (!batched && !unavailable) return batched;
        }

        CurlHandle h = NewHandle();
        if (!h)
            return UltraNetResult::Error(UltraNetResultCode::InsufficientMemory, "curl_easy_init failed");
        if (UltraNetResult a = ApplyCommonOptions(h.get(), options, tls); !a) return a;   // authenticate once; reuse below

        const std::string mbUrl = base + EncodeMailboxPath(folder);  // constant across UIDs
        for (uint32_t uid : rest) {
            std::ostringstream u; u << mbUrl << "/;UID=" << uid;
            std::string raw;
            // Bodies fetched ahead for the cache stay unread on the server.
            if (FetchKeepingUnread(h.get(), mbUrl, u.str(), uid, raw, nullptr) && !raw.empty())
                onMessage(uid, raw);
        }
        return UltraNetResult::Ok();
    }

    UltraNetResult StoreFlags(const std::string& serverUrl,
                              const std::string& folder,
                              uint32_t uid,
                              UltraNetMailFlags flags,
                              bool set,
                              const UltraNetMailOptions& options) override {
        std::string base; bool tls = false;
        if (!ParseServerBase(serverUrl, base, tls))
            return UltraNetResult::Error(UltraNetResultCode::InvalidUrl, "bad imap server URL");
        std::string tokens = FlagsToImapString(flags);
        if (tokens.empty())
            return UltraNetResult::Error(UltraNetResultCode::InvalidState, "no flags given");
        std::ostringstream cmd;
        cmd << "UID STORE " << uid << ' ' << (set ? '+' : '-') << "FLAGS (" << tokens << ')';
        // On the account's kept session: marking a message read signs in no more.
        bool unavailable = false;
        UltraNetResult stored = WithSession(base, tls, options, [&](ImapSession& s) {
            if (UltraNetResult sel = s.Select(folder); !sel) return sel;
            return s.Run(cmd.str());
        }, &unavailable);
        if (stored || !unavailable) return stored;
        std::string body;
        return RunCommand(base + EncodeMailboxPath(folder), cmd.str(), options, tls, body);
    }

    UltraNetResult ExpungeMessage(const std::string& serverUrl,
                                  const std::string& folder,
                                  uint32_t uid,
                                  const UltraNetMailOptions& options) override {
        std::string base; bool tls = false;
        if (!ParseServerBase(serverUrl, base, tls))
            return UltraNetResult::Error(UltraNetResultCode::InvalidUrl, "bad imap server URL");
        bool unavailable = false;
        UltraNetResult expunged = WithSession(base, tls, options, [&](ImapSession& s) {
            if (UltraNetResult sel = s.Select(folder); !sel) return sel;
            return s.Run(UidExpungeCommand(uid));
        }, &unavailable);
        if (expunged || !unavailable) return expunged;
        std::string body;
        return RunCommand(base + EncodeMailboxPath(folder), UidExpungeCommand(uid), options, tls,
                          body);
    }

    UltraNetResult MoveMessage(const std::string& serverUrl,
                               const std::string& srcFolder,
                               uint32_t uid,
                               const std::string& dstFolder,
                               const UltraNetMailOptions& options) override {
        std::string base; bool tls = false;
        if (!ParseServerBase(serverUrl, base, tls))
            return UltraNetResult::Error(UltraNetResultCode::InvalidUrl, "bad imap server URL");
        std::ostringstream cmd;
        cmd << "UID MOVE " << uid << " \"" << QuoteImapMailbox(dstFolder) << '"';
        bool unavailable = false;
        UltraNetResult moved = WithSession(base, tls, options, [&](ImapSession& s) {
            if (UltraNetResult sel = s.Select(srcFolder); !sel) return sel;
            return s.Run(cmd.str());
        }, &unavailable);
        if (moved || !unavailable) return moved;
        std::string body;
        return RunCommand(base + EncodeMailboxPath(srcFolder), cmd.str(), options, tls, body);
    }

    UltraNetResult AppendMessage(const std::string& serverUrl,
                                 const std::string& folder,
                                 const std::string& rawMessage,
                                 UltraNetMailFlags flags,
                                 const UltraNetMailOptions& options) override {
        // libcurl's APPEND takes no flags, so they are set right after it:
        // the message is found again by its Message-ID (libcurl does not
        // report the UID the server gave it) and stored with UID STORE.
        std::string base; bool tls = false;
        if (!ParseServerBase(serverUrl, base, tls))
            return UltraNetResult::Error(UltraNetResultCode::InvalidUrl, "bad imap server URL");

        CurlHandle h = NewHandle();
        if (!h)
            return UltraNetResult::Error(UltraNetResultCode::InsufficientMemory, "curl_easy_init failed");
        const std::string url = base + EncodeMailboxPath(folder);
        ReadCtx ctx{&rawMessage, 0};
        curl_easy_setopt(h.get(), CURLOPT_URL, url.c_str());
        curl_easy_setopt(h.get(), CURLOPT_UPLOAD, 1L);
        curl_easy_setopt(h.get(), CURLOPT_READFUNCTION, &ReadFromString);
        curl_easy_setopt(h.get(), CURLOPT_READDATA, &ctx);
        curl_easy_setopt(h.get(), CURLOPT_INFILESIZE_LARGE,
                         static_cast<curl_off_t>(rawMessage.size()));
        if (UltraNetResult a = ApplyCommonOptions(h.get(), options, tls); !a) return a;
        std::string why, diagnostics;
        CURLcode rc = ultranet_curlerror::Perform(h.get(), why, &diagnostics);
        if (rc != CURLE_OK)
            return ultranet_curlerror::Error(MapCurlError(rc), why, diagnostics);
        if (flags != UltraNetMailFlags::None) SetFlagsOfAppended(serverUrl, folder, rawMessage,
                                                                 flags, options);
        return UltraNetResult::Ok();
    }

    // Best effort: the message is uploaded whatever happens here - reporting a
    // failure would make the caller upload it a second time. A message
    // without a Message-ID cannot be found again and keeps no flags.
    void SetFlagsOfAppended(const std::string& serverUrl, const std::string& folder,
                            const std::string& rawMessage, UltraNetMailFlags flags,
                            const UltraNetMailOptions& options) {
        const std::string messageId = RawHeaderValue(rawMessage, "Message-ID");
        if (messageId.empty()) return;
        std::string base; bool tls = false;
        if (!ParseServerBase(serverUrl, base, tls)) return;
        std::string body;
        if (!RunCommand(base + EncodeMailboxPath(folder), SearchByMessageIdCommand(messageId),
                        options, tls, body))
            return;
        const std::vector<uint32_t> uids = ParseSearchUids(body);
        if (uids.empty()) return;
        // The newest copy: an earlier one with the same ID keeps its flags.
        const uint32_t uid = *std::max_element(uids.begin(), uids.end());
        StoreFlags(serverUrl, folder, uid, flags, /*set=*/true, options);
    }

    UltraNetResult FetchAllFlags(
        const std::string& serverUrl, const std::string& folder,
        const std::function<void(uint32_t uid, UltraNetMailFlags flags, bool flagsKnown)>& onFlags,
        const UltraNetMailOptions& options) override {
        std::string base; bool tls = false;
        if (!ParseServerBase(serverUrl, base, tls))
            return UltraNetResult::Error(UltraNetResultCode::InvalidUrl, "bad imap server URL");
        // On the account's kept session: the SEARCH, then one bulk FETCH of
        // every message's flags (best effort, as below).
        {
            std::vector<uint32_t> uids;
            std::unordered_map<uint32_t, UltraNetMailFlags> fm;
            bool unavailable = false;
            UltraNetResult listed = WithSession(base, tls, options, [&](ImapSession& s) {
                if (UltraNetResult ex = s.Examine(folder); !ex) return ex;
                std::string searchText;
                UltraNetResult sr = s.Run("UID SEARCH ALL", [&](const ImapResponse& r) {
                    searchText += r.Text() + "\r\n";
                });
                if (!sr) return sr;
                uids = ParseSearchUids(searchText);
                std::string flagsText;
                if (s.Run("UID FETCH 1:* (FLAGS)", [&](const ImapResponse& r) {
                        flagsText += r.Text() + "\r\n";
                    }))
                    for (const auto& pr : ParseAllFlags(flagsText)) fm[pr.first] = pr.second;
                return UltraNetResult::Ok();
            }, &unavailable);
            if (listed) {
                for (uint32_t uid : uids) {
                    auto it = fm.find(uid);
                    onFlags(uid, it != fm.end() ? it->second : UltraNetMailFlags::None,
                            /*flagsKnown=*/it != fm.end());
                }
                return UltraNetResult::Ok();
            }
            if (!unavailable) return listed;
        }
        const std::string mbUrl = base + EncodeMailboxPath(folder);

        // One reused connection for the whole pass (like FetchEnvelopes).
        CurlHandle h = NewHandle();
        if (!h)
            return UltraNetResult::Error(UltraNetResultCode::InsufficientMemory, "curl_easy_init failed");
        if (UltraNetResult a = ApplyCommonOptions(h.get(), options, tls); !a) return a;

        // The authoritative live-UID set comes from SEARCH, the same primitive
        // FetchEnvelopes uses successfully — NOT from parsing a bulk FLAGS fetch,
        // which can come back empty and would make the caller expunge everything.
        // A failed search returns an error so the caller skips its expunge pass.
        std::string searchBody;
        UltraNetResult sr = PerformOn(h.get(), mbUrl, "UID SEARCH ALL", searchBody);
        if (!sr) return sr;
        std::vector<uint32_t> uids = ParseSearchUids(searchBody);

        // Flags are best-effort: one bulk fetch, but a UID missing from the parsed
        // map is reported flagsKnown=false so the caller never rewrites its flags
        // on an unread guess. Its existence (for deletion detection) still stands.
        std::unordered_map<uint32_t, UltraNetMailFlags> fm;
        std::string flagsBody;
        if (PerformOn(h.get(), mbUrl, "UID FETCH 1:* (FLAGS)", flagsBody))
            for (const auto& pr : ParseAllFlags(flagsBody)) fm[pr.first] = pr.second;

        for (uint32_t uid : uids) {
            auto it = fm.find(uid);
            onFlags(uid, it != fm.end() ? it->second : UltraNetMailFlags::None,
                    /*flagsKnown=*/it != fm.end());
        }
        return UltraNetResult::Ok();
    }

private:
    // Legacy full imap URL (with mailbox) split, for FetchMessages.
    bool SplitMailboxUrl(const std::string& url, std::string& base,
                         std::string& mailbox, bool& tls) {
        UltraNetUrlComponents c;
        if (!UltraNet_ParseUrl(url, c)) return false;
        if (c.scheme != "imap" && c.scheme != "imaps") return false;
        tls = (c.scheme == "imaps");
        std::ostringstream os;
        os << c.scheme << "://";
        if (!c.username.empty()) {
            os << c.username;
            if (!c.password.empty()) os << ':' << c.password;
            os << '@';
        }
        os << c.host;
        if (c.port > 0) os << ':' << c.port;
        os << '/';
        base = os.str();
        mailbox = c.path;
        if (!mailbox.empty() && mailbox.front() == '/') mailbox.erase(0, 1);
        if (mailbox.empty()) mailbox = "INBOX";
        return true;
    }

    // Headers to a FETCH, and bodies by weight: a header is a few kilobytes,
    // a body can be megabytes, and every FETCH costs one round trip. Two
    // hundred headers are some 600 KB; a batch of bodies stops at 4 MB or a
    // hundred messages, a larger message going alone. Without the sizes, ten
    // bodies to a FETCH.
    static constexpr std::size_t kHeaderBatch      = 200;
    static constexpr std::size_t kBodyBatch        = 10;
    static constexpr std::size_t kBodyBatchMax     = 100;
    static constexpr uint64_t    kBodyBatchBytes   = 4u * 1024u * 1024u;
    // A kept session idle this long is asked a NOOP before it is used, and
    // given this long to answer: a dead connection must not hold up a check.
    static constexpr long long   kRecheckAfterMs   = 15000;
    static constexpr long        kAliveCheckMs     = 8000;

    // Run `work` on a signed-in session for the server and user of `base`
    // and `options`: one kept from an earlier call when there is one, else a
    // new one; it is kept again afterwards unless its connection broke. When
    // no session could be opened for a reason that is not the server's -
    // libcurl would not open one - `unavailable` is set, for the caller to
    // take the old, per-message way.
    UltraNetResult WithSession(const std::string& base, bool tls,
                               const UltraNetMailOptions& options,
                               const std::function<UltraNetResult(ImapSession&)>& work,
                               bool* unavailable = nullptr) {
        if (unavailable) *unavailable = false;
        const std::string key = SessionKey(base, tls, options);
        std::unique_ptr<ImapSession> session = Pool().Take(key);
        if (session) {
            session->SetTimeout(options.operationTimeoutMs);
            if (session->IdleMs() > kRecheckAfterMs && !session->StillAnswers(kAliveCheckMs))
                session.reset();
        }
        if (!session) {
            session = std::make_unique<ImapSession>();
            CURLcode code = CURLE_OK;
            UltraNetResult opened = session->Open(base, options, tls, &code);
            if (!opened) {
                if (unavailable) *unavailable = code != CURLE_OK && SessionUnavailable(code);
                return opened;
            }
        }
        const UltraNetResult r = work(*session);
        Pool().Give(key, std::move(session));
        return r;
    }

    // The flags and header of each of `uids` (newest first), kHeaderBatch to a
    // FETCH, handed on newest first. A message the server no longer has is
    // left out, as is one whose header came back empty. Returns the UIDs for
    // the caller to fetch one by one (empty when all went): those of a FETCH
    // the server refused and every one after it, and any whose header did not
    // come as a literal - a server may send a short one as a quoted string,
    // which the per-message fetch reads.
    std::vector<uint32_t> FetchHeadersBatched(
            ImapSession& session, const std::vector<uint32_t>& uids,
            const std::function<void(const UltraNetMailEnvelope&)>& onEnvelope) {
        std::vector<uint32_t> rest;
        for (std::size_t i = 0; i < uids.size(); i += kHeaderBatch) {
            const std::vector<uint32_t> batch(
                uids.begin() + static_cast<std::ptrdiff_t>(i),
                uids.begin() + static_cast<std::ptrdiff_t>(std::min(i + kHeaderBatch, uids.size())));
            std::vector<UltraNetMailEnvelope> got;
            std::vector<uint32_t> oneByOne;
            const UltraNetResult r = session.Run(
                "UID FETCH " + UidSetString(batch) + " (UID FLAGS BODY.PEEK[HEADER])",
                [&](const ImapResponse& response) {
                    ImapFetchItem item;
                    if (!ParseFetchResponse(response, item)) return;
                    if (std::find(batch.begin(), batch.end(), item.uid) == batch.end()) return;
                    const auto header = item.sections.find("BODY[HEADER]");
                    if (header == item.sections.end()) { oneByOne.push_back(item.uid); return; }
                    if (TrimWs(header->second).empty()) return;
                    UltraNetMailEnvelope env;
                    env.uid = item.uid;
                    env.flags = item.flags;
                    ParseEnvelopeHeaders(header->second, env);
                    got.push_back(std::move(env));
                });
            // Handed on only once the FETCH has completed, so a refused one
            // leaves nothing half-delivered for the fallback to repeat.
            if (!r) {
                rest.insert(rest.end(), uids.begin() + static_cast<std::ptrdiff_t>(i), uids.end());
                return rest;
            }
            std::sort(got.begin(), got.end(), [](const UltraNetMailEnvelope& a,
                                                 const UltraNetMailEnvelope& b) {
                return a.uid > b.uid;
            });
            if (onEnvelope) for (const auto& env : got) onEnvelope(env);
            rest.insert(rest.end(), oneByOne.begin(), oneByOne.end());
        }
        std::sort(rest.begin(), rest.end(), std::greater<uint32_t>());
        return rest;
    }

    // The whole text of each of `uids`, in batches of about kBodyBatchBytes
    // (their sizes asked for first, in one FETCH), each handed on as it
    // arrives. Returns the UIDs for the caller to fetch one by one (empty when
    // all went): those not yet handed on when the server refused a FETCH or
    // the connection broke, and any whose text did not come as a literal.
    std::vector<uint32_t> FetchBodiesBatched(
            ImapSession& session, const std::vector<uint32_t>& uids,
            const std::function<void(uint32_t uid, const std::string& raw)>& onMessage) {
        std::unordered_map<uint32_t, uint32_t> sizes;
        session.Run("UID FETCH " + UidSetString(uids) + " (UID RFC822.SIZE)",
                    [&](const ImapResponse& r) {
            ImapFetchItem item;
            if (ParseFetchResponse(r, item) && item.hasSize) sizes[item.uid] = item.size;
        });
        // In the order given (newest first, as the caller asks), cut by weight.
        std::vector<std::vector<uint32_t>> batches;
        std::vector<uint32_t> current;
        uint64_t weight = 0;
        const std::size_t maxCount = sizes.empty() ? kBodyBatch : kBodyBatchMax;
        for (uint32_t uid : uids) {
            const auto it = sizes.find(uid);
            const uint64_t size = it != sizes.end() ? it->second : 0;
            if (!current.empty() &&
                (current.size() >= maxCount || weight + size > kBodyBatchBytes)) {
                batches.push_back(std::move(current));
                current.clear();
                weight = 0;
            }
            current.push_back(uid);
            weight += size;
        }
        if (!current.empty()) batches.push_back(std::move(current));

        std::vector<uint32_t> oneByOne;
        std::vector<uint32_t> delivered;
        for (std::size_t b = 0; b < batches.size(); ++b) {
            const std::vector<uint32_t>& batch = batches[b];
            const UltraNetResult r = session.Run(
                "UID FETCH " + UidSetString(batch) + " (UID BODY.PEEK[])",
                [&](const ImapResponse& response) {
                    ImapFetchItem item;
                    if (!ParseFetchResponse(response, item)) return;
                    if (std::find(batch.begin(), batch.end(), item.uid) == batch.end()) return;
                    const auto body = item.sections.find("BODY[]");
                    if (body == item.sections.end()) { oneByOne.push_back(item.uid); return; }
                    if (body->second.empty()) return;
                    if (onMessage) onMessage(item.uid, body->second);
                    delivered.push_back(item.uid);
                });
            if (!r) {
                for (std::size_t k = b; k < batches.size(); ++k)
                    for (uint32_t uid : batches[k])
                        if (std::find(delivered.begin(), delivered.end(), uid) == delivered.end() &&
                            std::find(oneByOne.begin(), oneByOne.end(), uid) == oneByOne.end())
                            oneByOne.push_back(uid);
                return oneByOne;
            }
        }
        return oneByOne;
    }

    // The flags and header fields of each of `uids`, in that order, over the
    // one connection on `h`. A message whose header cannot be read - gone
    // since the search, or a fetch that failed - is not handed on: an empty
    // envelope would be stored as a blank row (no sender, no subject, no date)
    // that an incremental sync never asks for again.
    void FetchHeaders(CURL* h, const std::string& base, const std::string& folder,
                      const std::vector<uint32_t>& uids,
                      const std::function<void(const UltraNetMailEnvelope&)>& onEnvelope) {
        const std::string mbUrl = base + EncodeMailboxPath(folder);
        for (uint32_t uid : uids) {
            UltraNetMailEnvelope env;
            env.uid = uid;

            // Flags, then the header fields - read before the header, whose
            // fetch would otherwise have made every message \Seen already.
            std::string headerRaw;
            std::ostringstream hurl;
            hurl << mbUrl << "/;UID=" << uid << ";SECTION=HEADER";
            if (!FetchKeepingUnread(h, mbUrl, hurl.str(), uid, headerRaw, &env.flags) ||
                TrimWs(headerRaw).empty())
                continue;
            ParseEnvelopeHeaders(headerRaw, env);
            if (onEnvelope) onEnvelope(env);
        }
    }

    // Fetch `sectionUrl` (a message, or one section of it) on `h` without
    // marking the message read. libcurl turns a "/;UID=n[;SECTION=s]" URL into
    // "UID FETCH n BODY[s]", never BODY.PEEK[s], and RFC 3501 has the server set
    // \Seen for that - so listing or caching mail made all of it read, here and
    // in every other mail program. A custom "BODY.PEEK" command is no way round
    // it: libcurl hands on only the response lines that begin with '*', and the
    // message text is lost. So the flags are read first and, when the message
    // was unread, its \Seen is taken off again straight after. `flagsOut`, when
    // given, receives the flags as they were before the fetch.
    UltraNetResult FetchKeepingUnread(CURL* h, const std::string& mbUrl,
                                      const std::string& sectionUrl, uint32_t uid,
                                      std::string& out, UltraNetMailFlags* flagsOut) {
        std::string flagsBody;
        const bool known = PerformOn(h, mbUrl, UidFetchFlagsCommand(uid), flagsBody)
                           && HasFetchFlags(flagsBody);
        const UltraNetMailFlags before = known ? ParseFetchFlags(flagsBody)
                                               : UltraNetMailFlags::None;
        if (flagsOut) *flagsOut = before;
        UltraNetResult r = PerformOn(h, sectionUrl, std::string(), out);
        // Only when the flags were read: a message whose state is unknown is
        // left as the server has it rather than guessed unread.
        if (known && !UltraNetHasFlag(before, UltraNetMailFlags::Seen)) {
            std::string ignored;
            PerformOn(h, mbUrl, UidMarkUnreadCommand(uid), ignored);
        }
        return r;
    }
};

} // namespace

// v2 entry — preferred, works on Windows (host-vtable injection).
extern "C" ULTRANET_PLUGIN_EXPORT
void UltraNet_PluginInit(const UltraNetPluginHost* host) {
    // ABI 2: the core functions this plug-in calls come through `host`
    // (UltraNetPluginHostShim); an older host cannot serve them.
    if (!UltraNetPlugin_AttachHost(host)) return;
    host->RegisterPlugin(std::make_shared<ImapPlugin>());
}

