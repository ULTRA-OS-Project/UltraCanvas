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

#include "ImapParse.h"

#include <curl/curl.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace ultranet_imap;

using CurlHandle = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;
CurlHandle NewHandle() { return CurlHandle(curl_easy_init(), curl_easy_cleanup); }

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
    }
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(opt.connectTimeoutMs));
    curl_easy_setopt(h, CURLOPT_TIMEOUT_MS,        static_cast<long>(opt.operationTimeoutMs));
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
}

// Run a custom IMAP command against `url`, capturing the untagged response.
UltraNetResult RunCommand(const std::string& url, const std::string& customReq,
                          const UltraNetMailOptions& opt, bool implicitTls,
                          std::string& outBody) {
    CurlHandle h = NewHandle();
    if (!h)
        return UltraNetResult::Error(UltraNetResultCode::InsufficientMemory, "curl_easy_init failed");
    curl_easy_setopt(h.get(), CURLOPT_URL, url.c_str());
    if (!customReq.empty())
        curl_easy_setopt(h.get(), CURLOPT_CUSTOMREQUEST, customReq.c_str());
    curl_easy_setopt(h.get(), CURLOPT_WRITEFUNCTION, &WriteToString);
    curl_easy_setopt(h.get(), CURLOPT_WRITEDATA, &outBody);
    ApplyCommonOptions(h.get(), opt, implicitTls);
    CURLcode rc = curl_easy_perform(h.get());
    if (rc != CURLE_OK)
        return UltraNetResult::Error(MapCurlError(rc), curl_easy_strerror(rc));
    return UltraNetResult::Ok();
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
        std::string cmd = "STATUS \"" + folder +
            "\" (MESSAGES RECENT UIDNEXT UIDVALIDITY UNSEEN)";
        std::string body;
        UltraNetResult r = RunCommand(base, cmd, options, tls, body);
        if (!r) return r;
        outStatus = ParseStatusResponse(body);
        return UltraNetResult::Ok();
    }

    UltraNetResult FetchEnvelopes(const std::string& serverUrl,
                                  const std::string& folder,
                                  uint32_t sinceUid,
                                  std::vector<UltraNetMailEnvelope>& outEnvelopes,
                                  const UltraNetMailOptions& options) override {
        outEnvelopes.clear();
        std::string base; bool tls = false;
        if (!ParseServerBase(serverUrl, base, tls))
            return UltraNetResult::Error(UltraNetResultCode::InvalidUrl, "bad imap server URL");
        const std::string mbUrl = base + folder;

        std::ostringstream search;
        if (sinceUid > 0) search << "UID SEARCH UID " << (sinceUid + 1) << ":*";
        else              search << "UID SEARCH ALL";
        std::string searchBody;
        UltraNetResult sr = RunCommand(mbUrl, search.str(), options, tls, searchBody);
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
            hurl << base << folder << "/;UID=" << uid << ";SECTION=HEADER";
            if (RunFetch(hurl.str(), options, tls, headerRaw))
                ParseEnvelopeHeaders(headerRaw, env);

            // Flags.
            std::string flagsBody;
            std::ostringstream fcmd; fcmd << "UID FETCH " << uid << " (FLAGS)";
            if (RunCommand(mbUrl, fcmd.str(), options, tls, flagsBody))
                env.flags = ParseFetchFlags(flagsBody);

            outEnvelopes.push_back(std::move(env));
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
        std::ostringstream u; u << base << folder << "/;UID=" << uid;
        if (!RunFetch(u.str(), options, tls, outRaw))
            return UltraNetResult::Error(UltraNetResultCode::Unknown, "fetch failed");
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
        return RunCommand(base + folder, cmd.str(), options, tls, body);
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
        cmd << "UID MOVE " << uid << " \"" << dstFolder << '"';
        std::string body;
        return RunCommand(base + srcFolder, cmd.str(), options, tls, body);
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
        const std::string url = base + folder;
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

    // GET-style fetch (no custom request) capturing the message body.
    bool RunFetch(const std::string& url, const UltraNetMailOptions& opt,
                  bool tls, std::string& out) {
        CurlHandle h = NewHandle();
        if (!h) return false;
        curl_easy_setopt(h.get(), CURLOPT_URL, url.c_str());
        curl_easy_setopt(h.get(), CURLOPT_WRITEFUNCTION, &WriteToString);
        curl_easy_setopt(h.get(), CURLOPT_WRITEDATA, &out);
        ApplyCommonOptions(h.get(), opt, tls);
        return curl_easy_perform(h.get()) == CURLE_OK;
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
