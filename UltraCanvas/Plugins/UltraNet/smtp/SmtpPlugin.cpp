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
// Version: 0.1.1
// Last Modified: 2026-07-05
// Author: UltraCanvas Framework / ULTRA OS

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetPlugins.h>

#include <curl/curl.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace {

// ============================================================================
// Base64 encoder for attachment bodies. RFC 4648 §4, 76-column wrapping.
// ============================================================================
std::string Base64(const std::vector<uint8_t>& in) {
    static const char* kAlphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((in.size() + 2) / 3) * 4 + in.size() / 57 + 4);

    std::size_t lineWidth = 0;
    for (std::size_t i = 0; i < in.size(); i += 3) {
        const std::size_t left = in.size() - i;
        const uint32_t triplet =
            (static_cast<uint32_t>(in[i]) << 16) |
            (left > 1 ? static_cast<uint32_t>(in[i + 1]) << 8 : 0) |
            (left > 2 ? static_cast<uint32_t>(in[i + 2])      : 0);
        out.push_back(kAlphabet[(triplet >> 18) & 0x3f]);
        out.push_back(kAlphabet[(triplet >> 12) & 0x3f]);
        out.push_back(left > 1 ? kAlphabet[(triplet >> 6)  & 0x3f] : '=');
        out.push_back(left > 2 ? kAlphabet[ triplet        & 0x3f] : '=');
        lineWidth += 4;
        if (lineWidth >= 76) {
            out.append("\r\n");
            lineWidth = 0;
        }
    }
    if (lineWidth > 0) out.append("\r\n");
    return out;
}

// ============================================================================
// Helpers
// ============================================================================
std::string CommaJoin(const std::vector<std::string>& v) {
    std::string out;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i) out += ", ";
        out += v[i];
    }
    return out;
}

std::string AngleAddr(const std::string& addr) {
    if (!addr.empty() && addr.front() == '<' && addr.back() == '>') return addr;
    return "<" + addr + ">";
}

std::string Rfc2822Date() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[64];
    std::strftime(buf, sizeof buf, "%a, %d %b %Y %H:%M:%S +0000", &tm);
    return buf;
}

std::string GenerateBoundary() {
    static std::atomic<uint64_t> counter{0};
    std::ostringstream os;
    os << "----=_UltraNet_"
       << std::chrono::system_clock::now().time_since_epoch().count()
       << "_" << counter.fetch_add(1);
    return os.str();
}

std::string GenerateMessageId() {
    std::random_device rd;
    std::ostringstream os;
    os << "<" << std::hex << rd() << rd() << "@ultranet.local>";
    return os.str();
}

// ============================================================================
// RFC 822 / 2822 / 5322 message construction.
//
// For the no-attachment case: a flat message with the supplied content type.
// For the attachment case: a multipart/mixed wrapping a text/plain (or text/
// html) body and one base64-encoded application/octet-stream per attachment.
// ============================================================================
std::string BuildMessage(const UltraNetMailMessage& m) {
    std::ostringstream os;

    const bool hasAttachments = !m.attachments.empty();
    const std::string boundary = hasAttachments ? GenerateBoundary() : std::string{};
    const std::string bodyContentType =
        m.contentType.empty() ? "text/plain; charset=utf-8" : m.contentType;

    os << "From: "    << m.from                 << "\r\n";
    if (!m.to.empty())  os << "To: "  << CommaJoin(m.to)  << "\r\n";
    if (!m.cc.empty())  os << "Cc: "  << CommaJoin(m.cc)  << "\r\n";
    os << "Subject: " << m.subject               << "\r\n"
       << "Date: "    << Rfc2822Date()           << "\r\n"
       << "Message-ID: " << GenerateMessageId()  << "\r\n"
       << "MIME-Version: 1.0\r\n";

    for (const auto& [name, value] : m.headers) {
        // Skip headers we already generated.
        if (name == "MIME-Version" || name == "Date" || name == "Message-ID" ||
            name == "From" || name == "To" || name == "Cc" || name == "Bcc" ||
            name == "Subject" || name == "Content-Type" ||
            name == "Content-Transfer-Encoding") {
            continue;
        }
        os << name << ": " << value << "\r\n";
    }

    if (!hasAttachments) {
        os << "Content-Type: " << bodyContentType << "\r\n"
           << "Content-Transfer-Encoding: 8bit\r\n"
           << "\r\n"
           << m.body;
    } else {
        os << "Content-Type: multipart/mixed; boundary=\""
           << boundary << "\"\r\n\r\n"
           << "This is a multi-part message in MIME format.\r\n";

        // Body part.
        os << "--" << boundary << "\r\n"
           << "Content-Type: " << bodyContentType << "\r\n"
           << "Content-Transfer-Encoding: 8bit\r\n"
           << "\r\n"
           << m.body << "\r\n";

        // Attachments.
        for (const auto& [name, bytes] : m.attachments) {
            os << "--" << boundary << "\r\n"
               << "Content-Type: application/octet-stream; name=\""
                   << name << "\"\r\n"
               << "Content-Disposition: attachment; filename=\""
                   << name << "\"\r\n"
               << "Content-Transfer-Encoding: base64\r\n"
               << "\r\n"
               << Base64(bytes);
        }
        os << "--" << boundary << "--\r\n";
    }
    return os.str();
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
#if !defined(_WIN32) && !defined(_WIN64)  // v1 resolves UltraNet_RegisterPlugin from the host at dlopen(); POSIX-only, Windows uses the v2 UltraNet_PluginInit vtable above
extern "C" void UltraNet_PluginRegister(void) {
    UltraNet_RegisterPlugin(std::make_shared<SmtpPlugin>());
}
#endif
