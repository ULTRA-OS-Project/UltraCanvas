// UltraCanvas/Plugins/UltraNet/smtp/SmtpPlugin.cpp
// SMTP send-only plug-in. Implements IMailProtocolPlugin::SendMail using
// libcurl's native SMTP/SMTPS support. FetchMessages is delegated to a
// future IMAP / POP3 plug-in — this plug-in only sends.
//
// Build: produces libultranet_smtp.{so,dylib,dll}. Loaded by
// UltraNet_RefreshPlugins() at runtime. Entry point:
//
//   extern "C" void UltraNet_PluginRegister(void);
//
// This is the canonical reference implementation for the
// I<Category>ProtocolPlugin plug-in contract.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetPlugins.h>
#include <UltraNet/UltraNetMime.h>

#include <curl/curl.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

// ============================================================================
// Helpers
// ============================================================================
std::string AngleAddr(const std::string& addr) {
    if (!addr.empty() && addr.front() == '<' && addr.back() == '>') return addr;
    return "<" + addr + ">";
}

// ============================================================================
// Message construction — delegates to the shared UltraNet MIME builder
// (core UltraNet_MimeBuild) so the wire format lives in one place.
// ============================================================================
std::string BuildMessage(const UltraNetMailMessage& m) {
    UltraNetMimeBuildInput in;
    in.from = m.from;
    in.to   = m.to;
    in.cc   = m.cc;
    in.bcc  = m.bcc;
    in.subject = m.subject;
    in.body = m.body;
    in.extraHeaders = m.headers;

    // Split the caller's content type into media type + charset.
    if (!m.contentType.empty()) {
        std::size_t semi = m.contentType.find(';');
        in.bodyMediaType = m.contentType.substr(0, semi);
        while (!in.bodyMediaType.empty() && in.bodyMediaType.back() == ' ')
            in.bodyMediaType.pop_back();
        std::size_t cs = m.contentType.find("charset=");
        if (cs != std::string::npos) {
            std::string charset = m.contentType.substr(cs + 8);
            std::size_t end = charset.find(';');
            if (end != std::string::npos) charset = charset.substr(0, end);
            if (!charset.empty() && charset.front() == '"') charset = charset.substr(1);
            if (!charset.empty() && charset.back() == '"') charset.pop_back();
            in.bodyCharset = charset;
        }
    }

    for (const auto& [name, bytes] : m.attachments) {
        UltraNetMimeBuildAttachment a;
        a.filename = name;
        a.data = bytes;
        in.attachments.push_back(std::move(a));
    }

    return UltraNet_MimeBuild(in);
}

// ============================================================================
// libcurl read callback that feeds the constructed message bytes.
// ============================================================================
struct UploadCursor {
    const char* data;
    std::size_t remaining;
};

std::size_t ReadCallback(char* buffer, std::size_t size, std::size_t nmemb,
                         void* userp) {
    auto* u = static_cast<UploadCursor*>(userp);
    const std::size_t want = size * nmemb;
    const std::size_t n = want < u->remaining ? want : u->remaining;
    if (n == 0) return 0;
    std::memcpy(buffer, u->data, n);
    u->data      += n;
    u->remaining -= n;
    return n;
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

// ============================================================================
// SmtpPlugin
// ============================================================================
class SmtpPlugin : public IMailProtocolPlugin {
public:
    std::string GetName() const override   { return "UltraNet-SMTP"; }
    std::string GetVersion() const override { return "0.1.0"; }
    std::vector<std::string> GetSupportedSchemes() const override {
        return {"smtp", "smtps"};
    }
    UltraNetResult Initialize(const UltraNetConfig&) override {
        return UltraNetResult::Ok();
    }
    void Shutdown() override {}

    UltraNetResult SendMail(const UltraNetMailMessage& message,
                            const UltraNetMailOptions& options) override {
        if (options.serverUrl.empty()) {
            return UltraNetResult::Error(UltraNetResultCode::InvalidUrl,
                "UltraNetMailOptions::serverUrl is required for SMTP");
        }
        if (message.from.empty()) {
            return UltraNetResult::Error(UltraNetResultCode::InvalidState,
                                         "UltraNetMailMessage::from is empty");
        }
        if (message.to.empty() && message.cc.empty() && message.bcc.empty()) {
            return UltraNetResult::Error(UltraNetResultCode::InvalidState,
                                         "no recipients");
        }

        std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> h(
            curl_easy_init(), curl_easy_cleanup);
        if (!h) {
            return UltraNetResult::Error(UltraNetResultCode::InsufficientMemory,
                                         "curl_easy_init() failed");
        }

        curl_easy_setopt(h.get(), CURLOPT_URL, options.serverUrl.c_str());

        if (!options.credentials.username.empty()) {
            curl_easy_setopt(h.get(), CURLOPT_USERNAME,
                             options.credentials.username.c_str());
            curl_easy_setopt(h.get(), CURLOPT_PASSWORD,
                             options.credentials.password.c_str());
        }
        if (options.useTls && !options.implicitTls) {
            // STARTTLS upgrade (smtp://host:587)
            curl_easy_setopt(h.get(), CURLOPT_USE_SSL, CURLUSESSL_ALL);
        }

        const std::string fromAngle = AngleAddr(message.from);
        curl_easy_setopt(h.get(), CURLOPT_MAIL_FROM, fromAngle.c_str());

        curl_slist* recipients = nullptr;
        auto addAll = [&](const std::vector<std::string>& list) {
            for (const auto& addr : list) {
                recipients = curl_slist_append(recipients, AngleAddr(addr).c_str());
            }
        };
        addAll(message.to);
        addAll(message.cc);
        addAll(message.bcc);
        std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)>
            recGuard(recipients, curl_slist_free_all);
        curl_easy_setopt(h.get(), CURLOPT_MAIL_RCPT, recipients);

        const std::string payload = BuildMessage(message);
        UploadCursor cursor{payload.data(), payload.size()};
        curl_easy_setopt(h.get(), CURLOPT_READFUNCTION, &ReadCallback);
        curl_easy_setopt(h.get(), CURLOPT_READDATA,     &cursor);
        curl_easy_setopt(h.get(), CURLOPT_UPLOAD,       1L);
        curl_easy_setopt(h.get(), CURLOPT_INFILESIZE_LARGE,
                         static_cast<curl_off_t>(payload.size()));

        curl_easy_setopt(h.get(), CURLOPT_CONNECTTIMEOUT_MS,
                         static_cast<long>(options.connectTimeoutMs));
        curl_easy_setopt(h.get(), CURLOPT_TIMEOUT_MS,
                         static_cast<long>(options.operationTimeoutMs));
        curl_easy_setopt(h.get(), CURLOPT_NOSIGNAL, 1L);

        CURLcode rc = curl_easy_perform(h.get());
        if (rc == CURLE_OK) return UltraNetResult::Ok();
        return UltraNetResult::Error(MapCurlError(rc), curl_easy_strerror(rc));
    }

    UltraNetResult FetchMessages(const std::string&,
                                 std::vector<UltraNetMailMessage>&,
                                 const UltraNetMailOptions&) override {
        return UltraNetResult::Error(UltraNetResultCode::UnsupportedScheme,
            "SMTP plug-in only sends; install the IMAP / POP3 plug-in to fetch");
    }
};

} // namespace

// v2 entry — preferred, works on Windows. The host hands us a vtable so we
// don't need to resolve UltraNet_RegisterPlugin via load-time symbol lookup.
extern "C" ULTRANET_PLUGIN_EXPORT
void UltraNet_PluginInit(const UltraNetPluginHost* host) {
    if (!host || host->abiVersion < 1 || !host->RegisterPlugin) return;
    host->RegisterPlugin(std::make_shared<SmtpPlugin>());
}

// v1 entry — POSIX-only fallback for hosts that don't supply the v2 vtable.
extern "C" void UltraNet_PluginRegister(void) {
    UltraNet_RegisterPlugin(std::make_shared<SmtpPlugin>());
}
