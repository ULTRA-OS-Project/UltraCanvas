// UltraCanvas/Plugins/UltraNet/imap/ImapPlugin.cpp
// IMAP / IMAPS fetch-only plug-in. Implements IMailProtocolPlugin::
// FetchMessages on top of libcurl's native IMAP support. The companion
// SmtpPlugin handles SendMail; this plug-in returns UnsupportedScheme from
// SendMail with a pointer to its sibling.
//
// Build: produces libultranet_imap.{so,dylib}. Loaded at runtime by
// UltraNet_RefreshPlugins(). Entry point:
//   extern "C" void UltraNet_PluginRegister(void);
//
// Strategy:
//   1. Issue a SEARCH ALL against the mailbox URL to enumerate UIDs.
//   2. Fetch up to options.maxMessages most-recent messages by UID.
//   3. Parse minimal RFC 822 headers (From / To / Cc / Subject / Date /
//      Content-Type) into UltraNetMailMessage; full body lands in message.body.
// Version: 0.1.1
// Last Modified: 2026-07-05
// Author: UltraCanvas Framework / ULTRA OS

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetPlugins.h>
#include <UltraNet/UltraNetUrl.h>

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

// ============================================================================
// Helpers
// ============================================================================
std::size_t WriteToString(char* data, std::size_t size, std::size_t nmemb, void* ud) {
    auto* s = static_cast<std::string*>(ud);
    s->append(data, size * nmemb);
    return size * nmemb;
}

UltraNetResultCode MapCurlError(CURLcode rc) {
    switch (rc) {
        case CURLE_OK:                      return UltraNetResultCode::Success;
        case CURLE_URL_MALFORMAT:           return UltraNetResultCode::InvalidUrl;
        case CURLE_COULDNT_RESOLVE_HOST:    return UltraNetResultCode::HostNotFound;
        case CURLE_COULDNT_CONNECT:         return UltraNetResultCode::ConnectionRefused;
        case CURLE_OPERATION_TIMEDOUT:      return UltraNetResultCode::Timeout;
        case CURLE_LOGIN_DENIED:            return UltraNetResultCode::AuthenticationFailed;
        case CURLE_SSL_CONNECT_ERROR:       return UltraNetResultCode::TlsHandshakeFailed;
        case CURLE_PEER_FAILED_VERIFICATION:return UltraNetResultCode::TlsCertificateInvalid;
        default:                            return UltraNetResultCode::Unknown;
    }
}

std::string TrimLeft(const std::string& s) {
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}

// Splits a SEARCH response ("* SEARCH 1 2 3 4 5") into UIDs. The response
// may span multiple lines; we just look for the SEARCH-prefix line.
std::vector<int> ParseSearchUids(const std::string& body) {
    std::vector<int> uids;
    std::size_t pos = body.find("SEARCH");
    if (pos == std::string::npos) return uids;
    pos += 6;   // skip "SEARCH"
    while (pos < body.size()) {
        while (pos < body.size() && (body[pos] == ' ' || body[pos] == '\t')) ++pos;
        if (pos >= body.size() || body[pos] == '\r' || body[pos] == '\n') break;
        char* end = nullptr;
        long v = std::strtol(body.c_str() + pos, &end, 10);
        if (end == body.c_str() + pos) break;
        uids.push_back(static_cast<int>(v));
        pos = end - body.c_str();
    }
    return uids;
}

// Splits an address list ("a@b.com, c@d.com") into individual addresses.
void SplitAddrList(const std::string& src, std::vector<std::string>& out) {
    std::size_t i = 0;
    while (i < src.size()) {
        std::size_t comma = src.find(',', i);
        if (comma == std::string::npos) comma = src.size();
        std::string addr = src.substr(i, comma - i);
        // Trim
        while (!addr.empty() && (addr.front() == ' ' || addr.front() == '\t')) addr.erase(0, 1);
        while (!addr.empty() && (addr.back()  == ' ' || addr.back()  == '\t')) addr.pop_back();
        if (!addr.empty()) out.push_back(std::move(addr));
        i = comma + 1;
    }
}

// Minimal RFC 822 header parse — populates the From / To / Cc / Subject /
// Date / Content-Type fields and stuffs everything else into m.headers.
// `raw` is the full message text (headers + blank line + body).
void ParseMessage(const std::string& raw, UltraNetMailMessage& m) {
    std::size_t headerEnd = raw.find("\r\n\r\n");
    if (headerEnd == std::string::npos) headerEnd = raw.find("\n\n");
    const std::size_t bodyStart =
        (headerEnd == std::string::npos) ? raw.size()
                                         : headerEnd + (raw[headerEnd] == '\r' ? 4 : 2);
    const std::string headerBlock =
        (headerEnd == std::string::npos) ? raw : raw.substr(0, headerEnd);

    // Unfold continuation lines (headers split across multiple lines per
    // RFC 5322 §2.2.3 — continuation lines start with whitespace).
    std::string unfolded;
    {
        std::istringstream is(headerBlock);
        std::string line;
        while (std::getline(is, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty() && (line.front() == ' ' || line.front() == '\t') &&
                !unfolded.empty()) {
                unfolded += ' ';
                unfolded += TrimLeft(line);
            } else {
                if (!unfolded.empty()) unfolded += '\n';
                unfolded += line;
            }
        }
    }

    std::istringstream iss(unfolded);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.empty()) continue;
        std::size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string name  = line.substr(0, colon);
        std::string value = TrimLeft(line.substr(colon + 1));

        std::string lower; lower.reserve(name.size());
        for (char c : name) lower.push_back(static_cast<char>(std::tolower(c)));

        if (lower == "from")            m.from = value;
        else if (lower == "to")         SplitAddrList(value, m.to);
        else if (lower == "cc")         SplitAddrList(value, m.cc);
        else if (lower == "bcc")        SplitAddrList(value, m.bcc);
        else if (lower == "subject")    m.subject = value;
        else if (lower == "content-type") m.contentType = value;
        else m.headers[name] = value;
    }

    if (bodyStart < raw.size()) m.body = raw.substr(bodyStart);
}

// Builds the base URL for libcurl IMAP commands (server + mailbox, no UID).
// Input: "imap://user:pass@host:port/INBOX" -> {"imap://user:pass@host:port/", "INBOX"}.
bool ParseMailboxUrl(const std::string& url,
                     std::string& outBaseUrl,
                     std::string& outMailbox,
                     bool& outUseTls) {
    UltraNetUrlComponents c;
    if (!UltraNet_ParseUrl(url, c)) return false;
    if (c.scheme != "imap" && c.scheme != "imaps") return false;
    outUseTls = (c.scheme == "imaps");

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
    outBaseUrl = os.str();

    // Path begins with '/' — strip it for the mailbox name.
    outMailbox = c.path;
    if (!outMailbox.empty() && outMailbox.front() == '/') outMailbox.erase(0, 1);
    if (outMailbox.empty()) outMailbox = "INBOX";
    return true;
}

void ApplyCommonOptions(CURL* h, const UltraNetMailOptions& opt, bool implicitTls) {
    if (!opt.credentials.username.empty()) {
        curl_easy_setopt(h, CURLOPT_USERNAME, opt.credentials.username.c_str());
        curl_easy_setopt(h, CURLOPT_PASSWORD, opt.credentials.password.c_str());
    }
    if (opt.useTls || implicitTls) {
        curl_easy_setopt(h, CURLOPT_USE_SSL,
                         implicitTls ? CURLUSESSL_ALL : CURLUSESSL_TRY);
    }
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT_MS,
                     static_cast<long>(opt.connectTimeoutMs));
    curl_easy_setopt(h, CURLOPT_TIMEOUT_MS,
                     static_cast<long>(opt.operationTimeoutMs));
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
}

// ============================================================================
// ImapPlugin
// ============================================================================
class ImapPlugin : public IMailProtocolPlugin {
public:
    std::string GetName() const override   { return "UltraNet-IMAP"; }
    std::string GetVersion() const override { return "0.1.0"; }
    std::vector<std::string> GetSupportedSchemes() const override {
        return {"imap", "imaps"};
    }
    UltraNetResult Initialize(const UltraNetConfig&) override {
        return UltraNetResult::Ok();
    }
    void Shutdown() override {}

    UltraNetResult SendMail(const UltraNetMailMessage&,
                            const UltraNetMailOptions&) override {
        return UltraNetResult::Error(UltraNetResultCode::UnsupportedScheme,
            "IMAP plug-in only fetches; use the UltraNet-SMTP plug-in to send");
    }

    UltraNetResult FetchMessages(const std::string& url,
                                 std::vector<UltraNetMailMessage>& outMessages,
                                 const UltraNetMailOptions& options) override {
        outMessages.clear();

        std::string baseUrl, mailbox;
        bool implicitTls = false;
        if (!ParseMailboxUrl(url, baseUrl, mailbox, implicitTls)) {
            return UltraNetResult::Error(UltraNetResultCode::InvalidUrl,
                "expected imap:// or imaps:// URL with a mailbox path");
        }

        // -------- 1. SEARCH ALL: list UIDs in the mailbox --------
        std::vector<int> uids;
        {
            std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> h(
                curl_easy_init(), curl_easy_cleanup);
            if (!h) return UltraNetResult::Error(
                UltraNetResultCode::InsufficientMemory, "curl_easy_init failed");

            const std::string searchUrl = baseUrl + mailbox;
            std::string searchBody;
            curl_easy_setopt(h.get(), CURLOPT_URL, searchUrl.c_str());
            curl_easy_setopt(h.get(), CURLOPT_CUSTOMREQUEST, "SEARCH ALL");
            curl_easy_setopt(h.get(), CURLOPT_WRITEFUNCTION, &WriteToString);
            curl_easy_setopt(h.get(), CURLOPT_WRITEDATA, &searchBody);
            ApplyCommonOptions(h.get(), options, implicitTls);

            CURLcode rc = curl_easy_perform(h.get());
            if (rc != CURLE_OK) {
                return UltraNetResult::Error(MapCurlError(rc),
                                             curl_easy_strerror(rc));
            }
            uids = ParseSearchUids(searchBody);
        }

        if (uids.empty()) return UltraNetResult::Ok();

        // -------- 2. FETCH BODY[] for the N most-recent UIDs --------
        // SEARCH returns UIDs in ascending order; take the tail.
        std::size_t take = uids.size();
        if (options.maxMessages > 0 &&
            static_cast<std::size_t>(options.maxMessages) < take) {
            take = static_cast<std::size_t>(options.maxMessages);
        }
        for (std::size_t i = uids.size() - take; i < uids.size(); ++i) {
            std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> h(
                curl_easy_init(), curl_easy_cleanup);
            if (!h) continue;

            std::ostringstream fetchUrl;
            fetchUrl << baseUrl << mailbox << "/;UID=" << uids[i];
            std::string raw;
            curl_easy_setopt(h.get(), CURLOPT_URL, fetchUrl.str().c_str());
            curl_easy_setopt(h.get(), CURLOPT_WRITEFUNCTION, &WriteToString);
            curl_easy_setopt(h.get(), CURLOPT_WRITEDATA, &raw);
            ApplyCommonOptions(h.get(), options, implicitTls);

            if (curl_easy_perform(h.get()) != CURLE_OK || raw.empty()) continue;

            UltraNetMailMessage msg;
            ParseMessage(raw, msg);
            outMessages.push_back(std::move(msg));
        }
        return UltraNetResult::Ok();
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
