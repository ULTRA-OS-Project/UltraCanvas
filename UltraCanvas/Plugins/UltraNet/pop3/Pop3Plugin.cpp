// UltraCanvas/Plugins/UltraNet/pop3/Pop3Plugin.cpp
// POP3 / POP3S fetch-only plug-in. Implements IMailProtocolPlugin::
// FetchMessages on libcurl's native POP3 support. Companion to the SMTP
// (send) and IMAP (fetch with mailbox/folder semantics) plug-ins — POP3
// is the simpler "single inbox, sequential numbering" alternative.
//
// Build: produces libultranet_pop3.{so,dylib}. Loaded at runtime by
// UltraNet_RefreshPlugins(). Entry point:
//   extern "C" void UltraNet_PluginRegister(void);
//
// Strategy:
//   1. LIST against pop3://server/ to discover message count.
//   2. Fetch the N most-recent messages (highest index first) via
//      pop3://server/N URLs, up to options.maxMessages.
//   3. Parse minimal RFC 822 headers; full raw message lands in
//      UltraNetMailMessage::body.
//
// We never DELE — apps that want to delete must do it explicitly.
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
// Helpers — shared shape with the IMAP plug-in for consistency.
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

void SplitAddrList(const std::string& src, std::vector<std::string>& out) {
    std::size_t i = 0;
    while (i < src.size()) {
        std::size_t comma = src.find(',', i);
        if (comma == std::string::npos) comma = src.size();
        std::string addr = src.substr(i, comma - i);
        while (!addr.empty() && (addr.front() == ' ' || addr.front() == '\t')) addr.erase(0, 1);
        while (!addr.empty() && (addr.back()  == ' ' || addr.back()  == '\t')) addr.pop_back();
        if (!addr.empty()) out.push_back(std::move(addr));
        i = comma + 1;
    }
}

void ParseMessage(const std::string& raw, UltraNetMailMessage& m) {
    std::size_t headerEnd = raw.find("\r\n\r\n");
    if (headerEnd == std::string::npos) headerEnd = raw.find("\n\n");
    const std::size_t bodyStart =
        (headerEnd == std::string::npos) ? raw.size()
                                         : headerEnd + (raw[headerEnd] == '\r' ? 4 : 2);
    const std::string headerBlock =
        (headerEnd == std::string::npos) ? raw : raw.substr(0, headerEnd);

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

        if      (lower == "from")         m.from = value;
        else if (lower == "to")           SplitAddrList(value, m.to);
        else if (lower == "cc")           SplitAddrList(value, m.cc);
        else if (lower == "bcc")          SplitAddrList(value, m.bcc);
        else if (lower == "subject")      m.subject = value;
        else if (lower == "content-type") m.contentType = value;
        else m.headers[name] = value;
    }
    if (bodyStart < raw.size()) m.body = raw.substr(bodyStart);
}

// Parses the LIST response: each line is "<num> <bytes>", terminated by a
// "." line. Returns the highest message number seen.
int CountMessagesInListResponse(const std::string& body) {
    int maxIdx = 0;
    std::istringstream is(body);
    std::string line;
    while (std::getline(is, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line == ".") continue;
        char* end = nullptr;
        long v = std::strtol(line.c_str(), &end, 10);
        if (end != line.c_str() && v > maxIdx) maxIdx = static_cast<int>(v);
    }
    return maxIdx;
}

// Builds the base URL ("pop3://[user:pass@]host[:port]/") and tells caller
// whether the scheme was the implicit-TLS variant.
bool ParsePop3Url(const std::string& url,
                  std::string& outBaseUrl,
                  bool& outImplicitTls) {
    UltraNetUrlComponents c;
    if (!UltraNet_ParseUrl(url, c)) return false;
    if (c.scheme != "pop3" && c.scheme != "pop3s") return false;
    outImplicitTls = (c.scheme == "pop3s");

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
// Pop3Plugin
// ============================================================================
class Pop3Plugin : public IMailProtocolPlugin {
public:
    std::string GetName() const override   { return "UltraNet-POP3"; }
    std::string GetVersion() const override { return "0.1.0"; }
    std::vector<std::string> GetSupportedSchemes() const override {
        return {"pop3", "pop3s"};
    }
    UltraNetResult Initialize(const UltraNetConfig&) override {
        return UltraNetResult::Ok();
    }
    void Shutdown() override {}

    UltraNetResult SendMail(const UltraNetMailMessage&,
                            const UltraNetMailOptions&) override {
        return UltraNetResult::Error(UltraNetResultCode::UnsupportedScheme,
            "POP3 plug-in only fetches; use the UltraNet-SMTP plug-in to send");
    }

    UltraNetResult FetchMessages(const std::string& url,
                                 std::vector<UltraNetMailMessage>& outMessages,
                                 const UltraNetMailOptions& options) override {
        outMessages.clear();

        std::string baseUrl;
        bool implicitTls = false;
        if (!ParsePop3Url(url, baseUrl, implicitTls)) {
            return UltraNetResult::Error(UltraNetResultCode::InvalidUrl,
                "expected pop3:// or pop3s:// URL");
        }

        // -------- 1. LIST: discover message count --------
        int totalMessages = 0;
        {
            std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> h(
                curl_easy_init(), curl_easy_cleanup);
            if (!h) return UltraNetResult::Error(
                UltraNetResultCode::InsufficientMemory, "curl_easy_init failed");

            std::string listBody;
            curl_easy_setopt(h.get(), CURLOPT_URL, baseUrl.c_str());
            curl_easy_setopt(h.get(), CURLOPT_WRITEFUNCTION, &WriteToString);
            curl_easy_setopt(h.get(), CURLOPT_WRITEDATA, &listBody);
            ApplyCommonOptions(h.get(), options, implicitTls);

            CURLcode rc = curl_easy_perform(h.get());
            if (rc != CURLE_OK) {
                return UltraNetResult::Error(MapCurlError(rc),
                                             curl_easy_strerror(rc));
            }
            totalMessages = CountMessagesInListResponse(listBody);
        }
        if (totalMessages <= 0) return UltraNetResult::Ok();

        // -------- 2. RETR the N highest-numbered messages (most recent) --------
        int take = totalMessages;
        if (options.maxMessages > 0 && options.maxMessages < take) {
            take = options.maxMessages;
        }
        const int firstIdx = totalMessages - take + 1;
        for (int i = firstIdx; i <= totalMessages; ++i) {
            std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> h(
                curl_easy_init(), curl_easy_cleanup);
            if (!h) continue;

            const std::string retrUrl = baseUrl + std::to_string(i);
            std::string raw;
            curl_easy_setopt(h.get(), CURLOPT_URL, retrUrl.c_str());
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
    host->RegisterPlugin(std::make_shared<Pop3Plugin>());
}

// v1 entry — POSIX-only fallback.
#if !defined(_WIN32) && !defined(_WIN64)  // v1 resolves UltraNet_RegisterPlugin from the host at dlopen(); POSIX-only, Windows uses the v2 UltraNet_PluginInit vtable above
extern "C" void UltraNet_PluginRegister(void) {
    UltraNet_RegisterPlugin(std::make_shared<Pop3Plugin>());
}
#endif
