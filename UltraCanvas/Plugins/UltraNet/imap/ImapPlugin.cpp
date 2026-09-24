// UltraCanvas/Plugins/UltraNet/imap/ImapPlugin.cpp
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
//   extern "C" void UltraNet_PluginRegister(void);
//
// Strategy:
//   1. Issue a SEARCH ALL against the mailbox URL to enumerate UIDs.
//   2. Fetch up to options.maxMessages most-recent messages by UID.
//   3. Parse minimal RFC 822 headers (From / To / Cc / Subject / Date /
//      Content-Type) into UltraNetMailMessage; full body lands in message.body.

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetPlugins.h>
#include <UltraNet/UltraNetUrl.h>
#include <UltraNet/UltraNetCurlDebug.h>

#include "ImapParse.h"

#include <curl/curl.h>

#include <algorithm>
#include <cstring>
#include <memory>
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

void ApplyCommonOptions(CURL* h, const UltraNetMailOptions& opt, bool implicitTls) {
    ultranet_curldebug::EnableIfRequested(h);
    const auto& cred = opt.credentials;
    const bool useBearer = !cred.token.empty() &&
        (cred.type == UltraNetAuthType::OAuth2 || cred.type == UltraNetAuthType::Bearer);
    if (useBearer) {
        // XOAUTH2 for Gmail / Microsoft: username + bearer token.
        if (!cred.username.empty())
            curl_easy_setopt(h, CURLOPT_USERNAME, cred.username.c_str());
        curl_easy_setopt(h, CURLOPT_XOAUTH2_BEARER, cred.token.c_str());
    } else if (!cred.username.empty()) {
        curl_easy_setopt(h, CURLOPT_USERNAME, cred.username.c_str());
        curl_easy_setopt(h, CURLOPT_PASSWORD, cred.password.c_str());
    }
    if (opt.useTls || implicitTls) {
        curl_easy_setopt(h, CURLOPT_USE_SSL,
                         implicitTls ? CURLUSESSL_ALL : CURLUSESSL_TRY);
        // Trust the same CA anchors as the HTTP client. Matters on Windows,
        // where the system libcurl's baked-in CA path does not exist on an end
        // user's machine; empty leaves libcurl's own default in place.
        const std::string caBundle = UltraNet_ResolveCaBundlePath();
        if (!caBundle.empty())
            curl_easy_setopt(h, CURLOPT_CAINFO, caBundle.c_str());
#if defined(_WIN32) && defined(CURLSSLOPT_NATIVE_CA)
        curl_easy_setopt(h, CURLOPT_SSL_OPTIONS, static_cast<long>(CURLSSLOPT_NATIVE_CA));
#endif
    }
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(opt.connectTimeoutMs));
    curl_easy_setopt(h, CURLOPT_TIMEOUT_MS,        static_cast<long>(opt.operationTimeoutMs));
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
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
    CURLcode rc = curl_easy_perform(h);
    if (rc != CURLE_OK)
        return UltraNetResult::Error(MapCurlError(rc), curl_easy_strerror(rc));
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
    ApplyCommonOptions(h.get(), opt, implicitTls);
    return PerformOn(h.get(), url, customReq, outBody);
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
    std::string GetVersion() const override { return "0.2.0"; }
    std::vector<std::string> GetSupportedSchemes() const override {
        return {"imap", "imaps"};
    }
    UltraNetResult Initialize(const UltraNetConfig&) override { return UltraNetResult::Ok(); }
    void Shutdown() override {}

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
        std::string searchBody;
        UltraNetResult sr = RunCommand(base + mailbox, "SEARCH ALL", options, tls, searchBody);
        if (!sr) return sr;
        std::vector<uint32_t> uids = ParseSearchUids(searchBody);
        if (uids.empty()) return UltraNetResult::Ok();

        std::size_t take = uids.size();
        if (options.maxMessages > 0 && static_cast<std::size_t>(options.maxMessages) < take)
            take = static_cast<std::size_t>(options.maxMessages);
        for (std::size_t i = uids.size() - take; i < uids.size(); ++i) {
            std::string raw;
            std::ostringstream u; u << base << mailbox << "/;UID=" << uids[i];
            if (!RunFetch(u.str(), options, tls, raw) || raw.empty()) continue;
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
        const std::string mbUrl = base + EncodeMailboxPath(folder);

        // One handle for the whole pass: the SEARCH and every per-UID header/flags
        // fetch reuse the same authenticated connection instead of reconnecting.
        CurlHandle h = NewHandle();
        if (!h)
            return UltraNetResult::Error(UltraNetResultCode::InsufficientMemory, "curl_easy_init failed");
        ApplyCommonOptions(h.get(), options, tls);

        std::ostringstream search;
        if (sinceUid > 0) search << "UID SEARCH UID " << (sinceUid + 1) << ":*";
        else              search << "UID SEARCH ALL";
        std::string searchBody;
        UltraNetResult sr = PerformOn(h.get(), mbUrl, search.str(), searchBody);
        if (!sr) return sr;
        std::vector<uint32_t> uids = ParseSearchUids(searchBody);
        // Newest first, bounded by maxMessages.
        std::sort(uids.begin(), uids.end(), std::greater<uint32_t>());
        if (options.maxMessages > 0 &&
            static_cast<std::size_t>(options.maxMessages) < uids.size())
            uids.resize(static_cast<std::size_t>(options.maxMessages));

        for (uint32_t uid : uids) {
            UltraNetMailEnvelope env;
            env.uid = uid;

            // Header fields.
            std::string headerRaw;
            std::ostringstream hurl;
            hurl << base << EncodeMailboxPath(folder) << "/;UID=" << uid << ";SECTION=HEADER";
            if (PerformOn(h.get(), hurl.str(), std::string(), headerRaw))
                ParseEnvelopeHeaders(headerRaw, env);

            // Flags.
            std::string flagsBody;
            std::ostringstream fcmd; fcmd << "UID FETCH " << uid << " (FLAGS)";
            if (PerformOn(h.get(), mbUrl, fcmd.str(), flagsBody))
                env.flags = ParseFetchFlags(flagsBody);

            if (onEnvelope) onEnvelope(env);
        }
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
        std::ostringstream u; u << base << EncodeMailboxPath(folder) << "/;UID=" << uid;
        if (!RunFetch(u.str(), options, tls, outRaw))
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

        CurlHandle h = NewHandle();
        if (!h)
            return UltraNetResult::Error(UltraNetResultCode::InsufficientMemory, "curl_easy_init failed");
        ApplyCommonOptions(h.get(), options, tls);   // authenticate once; reuse below

        const std::string mbPath = EncodeMailboxPath(folder);  // constant across UIDs
        for (uint32_t uid : uids) {
            std::ostringstream u; u << base << mbPath << "/;UID=" << uid;
            std::string raw;
            if (PerformOn(h.get(), u.str(), std::string(), raw) && !raw.empty())
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
        std::string body;
        return RunCommand(base + EncodeMailboxPath(folder), cmd.str(), options, tls, body);
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
        std::string body;
        return RunCommand(base + EncodeMailboxPath(srcFolder), cmd.str(), options, tls, body);
    }

    UltraNetResult AppendMessage(const std::string& serverUrl,
                                 const std::string& folder,
                                 const std::string& rawMessage,
                                 UltraNetMailFlags /*flags*/,
                                 const UltraNetMailOptions& options) override {
        // NOTE: flags on APPEND are not transmitted in this pass (libcurl has no
        // direct APPEND-flags option); callers can StoreFlags afterwards.
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
        ApplyCommonOptions(h.get(), options, tls);
        CURLcode rc = curl_easy_perform(h.get());
        if (rc != CURLE_OK)
            return UltraNetResult::Error(MapCurlError(rc), curl_easy_strerror(rc));
        return UltraNetResult::Ok();
    }

    UltraNetResult FetchAllFlags(
        const std::string& serverUrl, const std::string& folder,
        const std::function<void(uint32_t uid, UltraNetMailFlags flags, bool flagsKnown)>& onFlags,
        const UltraNetMailOptions& options) override {
        std::string base; bool tls = false;
        if (!ParseServerBase(serverUrl, base, tls))
            return UltraNetResult::Error(UltraNetResultCode::InvalidUrl, "bad imap server URL");
        const std::string mbUrl = base + EncodeMailboxPath(folder);

        // One reused connection for the whole pass (like FetchEnvelopes).
        CurlHandle h = NewHandle();
        if (!h)
            return UltraNetResult::Error(UltraNetResultCode::InsufficientMemory, "curl_easy_init failed");
        ApplyCommonOptions(h.get(), options, tls);

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

    // GET-style fetch (no custom request) capturing the message body, on its own
    // one-off connection. FetchEnvelopes / FetchMessageBodies use PerformOn on a
    // shared handle instead, to avoid a reconnect per message.
    bool RunFetch(const std::string& url, const UltraNetMailOptions& opt,
                  bool tls, std::string& out) {
        CurlHandle h = NewHandle();
        if (!h) return false;
        ApplyCommonOptions(h.get(), opt, tls);
        return static_cast<bool>(PerformOn(h.get(), url, std::string(), out));
    }
};

} // namespace

// v2 entry — preferred, works on Windows (host-vtable injection).
extern "C" ULTRANET_PLUGIN_EXPORT
void UltraNet_PluginInit(const UltraNetPluginHost* host) {
    if (!host || host->abiVersion < 1 || !host->RegisterPlugin) return;
    host->RegisterPlugin(std::make_shared<ImapPlugin>());
}

// v1 entry — POSIX-only fallback.
#if !defined(_WIN32) && !defined(_WIN64)  // v1 resolves UltraNet_RegisterPlugin from the host at dlopen(); POSIX-only, Windows uses the v2 UltraNet_PluginInit vtable above
extern "C" void UltraNet_PluginRegister(void) {
    UltraNet_RegisterPlugin(std::make_shared<ImapPlugin>());
}
#endif
