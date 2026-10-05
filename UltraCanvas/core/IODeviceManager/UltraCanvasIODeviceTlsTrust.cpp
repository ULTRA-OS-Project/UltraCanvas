// core/IODeviceManager/UltraCanvasIODeviceTlsTrust.cpp
// Trust on first use for network devices' self-signed certificates. See
// UltraCanvasIODeviceTlsTrust.h.
// Version: 1.0.0
// Author: UltraCanvas Framework / ULTRA OS

#include "IODeviceManager/UltraCanvasIODeviceTlsTrust.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasSettingsFolder.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>
#include <system_error>

namespace UltraCanvas {

namespace {

constexpr const char* kPinPrefix = "sha256//";

std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string Trim(const std::string& text) {
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return std::string();
    const size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

bool AllDigits(const std::string& text) {
    return !text.empty() && std::all_of(text.begin(), text.end(), [](unsigned char c) {
        return std::isdigit(c) != 0;
    });
}

// Whoever reads and writes the file in this process goes through here, so a
// learned key and a forgotten one cannot overwrite each other. Another
// application writing at the same moment can still lose a line - the cost is
// one more first contact, never a wrong key.
std::mutex& FileMutex() {
    static std::mutex mutex;
    return mutex;
}

std::map<std::string, std::string> LoadPins() {
    const std::filesystem::path path = IODeviceTrustedCertificatesFile();
    if (path.empty()) return {};
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream text;
    text << in.rdbuf();
    return ParseIODeviceTrustedCertificates(text.str());
}

bool SavePins(const std::map<std::string, std::string>& pins) {
    const std::filesystem::path path = IODeviceTrustedCertificatesFile();
    if (path.empty()) return false;
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    // Written beside the file and renamed over it, so a reader never sees
    // half a list.
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << FormatIODeviceTrustedCertificates(pins);
        if (!out.flush()) return false;
    }
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        // Some platforms will not rename over an existing file.
        std::filesystem::remove(path, ec);
        std::filesystem::rename(temporary, path, ec);
    }
    return !ec;
}

}  // namespace

// ============================================================================
// PURE HELPERS
// ============================================================================

std::string IODeviceTlsAddress(const std::string& urlOrAddress) {
    std::string rest = Trim(urlOrAddress);
    const size_t scheme = rest.find("://");
    if (scheme != std::string::npos) {
        if (Lower(rest.substr(0, scheme)) != "https") return std::string();
        rest = rest.substr(scheme + 3);
        const size_t end = rest.find_first_of("/?#");
        if (end != std::string::npos) rest = rest.substr(0, end);
        const size_t at = rest.rfind('@');
        if (at != std::string::npos) rest = rest.substr(at + 1);
    } else if (rest.find_first_of("/?#@") != std::string::npos) {
        return std::string();
    }
    if (rest.empty()) return std::string();

    std::string host;
    std::string port;
    if (rest.front() == '[') {
        const size_t close = rest.find(']');
        if (close == std::string::npos || close == 1) return std::string();
        host = rest.substr(0, close + 1);
        const std::string after = rest.substr(close + 1);
        if (!after.empty()) {
            if (after.front() != ':') return std::string();
            port = after.substr(1);
        }
    } else {
        const size_t colon = rest.find(':');
        host = rest.substr(0, colon);
        if (colon != std::string::npos) {
            port = rest.substr(colon + 1);
            if (port.find(':') != std::string::npos) return std::string();
        }
    }
    while (!host.empty() && host.back() == '.') host.pop_back();
    if (host.empty()) return std::string();

    if (port.empty()) {
        // A bare "host" is not an address of a TLS device; only a URL may
        // leave the port to its scheme.
        if (scheme == std::string::npos) return std::string();
        port = "443";
    }
    if (!AllDigits(port) || port.size() > 5) return std::string();
    const long number = std::strtol(port.c_str(), nullptr, 10);
    if (number <= 0 || number > 65535) return std::string();
    return Lower(host) + ":" + std::to_string(number);
}

std::map<std::string, std::string> ParseIODeviceTrustedCertificates(const std::string& text) {
    std::map<std::string, std::string> pins;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        line = Trim(line);
        if (line.empty() || line.front() == '#') continue;
        // The address may hold colons (a port, an IPv6 literal) but never
        // '=', so the first '=' ends it.
        const size_t equals = line.find('=');
        if (equals == std::string::npos) continue;
        const std::string address = IODeviceTlsAddress(Trim(line.substr(0, equals)));
        const std::string pin = Trim(line.substr(equals + 1));
        if (address.empty() || pin.rfind(kPinPrefix, 0) != 0 ||
            pin.size() == std::string(kPinPrefix).size()) {
            continue;
        }
        pins[address] = pin;
    }
    return pins;
}

std::string FormatIODeviceTrustedCertificates(const std::map<std::string, std::string>& pins) {
    std::ostringstream out;
    out << "# Network devices whose self-signed certificate UltraCanvas trusts,\n"
           "# learned the first time each was reached: host:port=sha256//<key hash>.\n"
           "# Delete a device's line when it was reset or replaced and now presents\n"
           "# a new certificate; its new key is learned on the next connection.\n";
    for (const auto& [address, pin] : pins) out << address << '=' << pin << '\n';
    return out.str();
}

// ============================================================================
// THE STORE
// ============================================================================

std::filesystem::path IODeviceTrustedCertificatesFile() {
#if defined(_WIN32) || defined(_WIN64)
    if (const wchar_t* named = _wgetenv(L"ULTRACANVAS_DEVICE_CERTIFICATES"); named && *named)
        return std::filesystem::path(named);   // path-string-ok: wide
#else
    if (const char* named = std::getenv("ULTRACANVAS_DEVICE_CERTIFICATES"); named && *named)
        return PathFromUtf8(named);
#endif
    const std::filesystem::path folder = UltraCanvasSettingsFolder();
    if (folder.empty()) return {};
    return folder / "DeviceCertificates.conf";
}

std::vector<IODeviceTrustedCertificate> IODeviceTrustedCertificates() {
    std::lock_guard<std::mutex> lock(FileMutex());
    std::vector<IODeviceTrustedCertificate> list;
    for (const auto& [address, pin] : LoadPins()) list.push_back({address, pin});
    return list;
}

bool IODeviceForgetCertificate(const std::string& address) {
    const std::string key = IODeviceTlsAddress(address);
    if (key.empty()) return false;
    std::lock_guard<std::mutex> lock(FileMutex());
    std::map<std::string, std::string> pins = LoadPins();
    if (pins.erase(key) == 0) return false;
    return SavePins(pins);
}

}  // namespace UltraCanvas

// ============================================================================
// THE REQUEST
// ============================================================================

#if defined(ULTRACANVAS_HAS_NET)

namespace UltraCanvas {

namespace {

bool LearningAllowed() {
    const char* setting = std::getenv("ULTRACANVAS_DEVICE_TLS_TOFU");
    return !(setting && std::string(setting) == "0");
}

std::string PinFor(const std::string& address) {
    std::lock_guard<std::mutex> lock(FileMutex());
    const std::map<std::string, std::string> pins = LoadPins();
    auto found = pins.find(address);
    return found == pins.end() ? std::string() : found->second;
}

// Keeps `pin` for `address` unless one is kept already - learned meanwhile by
// another request - and returns whichever is kept. A key is never replaced
// here: that is IODeviceForgetCertificate's to do.
std::string Remember(const std::string& address, const std::string& pin) {
    std::lock_guard<std::mutex> lock(FileMutex());
    std::map<std::string, std::string> pins = LoadPins();
    auto found = pins.find(address);
    if (found != pins.end()) return found->second;
    pins[address] = pin;
    SavePins(pins);   // unsaved, it is learned again next time - no harm
    return pin;
}

// The device's public key, read from a connection that asks for nothing:
// a HEAD of its root, with no headers or body of the real request, so the
// unverified peer learns nothing from it. Empty when it could not be read.
std::string FetchPin(const std::string& url, const UltraNetHttpOptions& original) {
    const size_t scheme = url.find("://");
    const size_t pathStart = url.find('/', scheme == std::string::npos ? 0 : scheme + 3);
    UltraNetHttpRequest probe;
    probe.url = (pathStart == std::string::npos ? url : url.substr(0, pathStart)) + "/";
    probe.method = UltraNetHttpMethod::Head;
    probe.options.acceptInvalidCert = true;
    probe.options.capturePeerCertificate = true;
    probe.options.followRedirects = false;
    probe.options.connectTimeoutMs = original.connectTimeoutMs > 0 ? original.connectTimeoutMs : 5000;
    probe.options.timeoutMs = 10000;

    UltraNetResponse response;
    UltraNet_HttpRequest(probe, response);   // the answer does not matter, the handshake does
    return response.tlsInfo.peerPublicKeyPin;
}

std::string ForgetHint(const std::string& address) {
    const std::filesystem::path file = IODeviceTrustedCertificatesFile();
    return " If the device was reset or replaced, forget its old key - in UOS-Settings "
           "under Devices > Trusted certificates, or by deleting its line from " +
           (file.empty() ? std::string("DeviceCertificates.conf") : PathToUtf8(file)) +
           " (IODeviceForgetCertificate(\"" + address + "\") in code) - and it is learned "
           "again on the next connection.";
}

}  // namespace

namespace Internal {

UltraNetResult DeviceHttpRequest(UltraNetHttpRequest request, UltraNetResponse& response) {
    const std::string address = IODeviceTlsAddress(request.url);
    if (address.empty()) return UltraNet_HttpRequest(request, response);

    std::string pin = PinFor(address);
    if (pin.empty()) {
        // Ordinary verification first: a device with a certificate someone
        // vouches for needs nothing remembered, and pinning it would break
        // the day it renews.
        UltraNetResult verified = UltraNet_HttpRequest(request, response);
        if (verified.success || verified.code != UltraNetResultCode::TlsCertificateInvalid) {
            return verified;
        }
        if (!LearningAllowed()) {
            verified.message += " - the device's certificate is not one anybody vouches for, "
                                "and learning new device keys is switched off "
                                "(ULTRACANVAS_DEVICE_TLS_TOFU=0)";
            return verified;
        }
        const std::string learned = FetchPin(request.url, request.options);
        if (learned.empty()) {
            verified.message += " - and the device's certificate could not be read to trust "
                                "it on first use";
            return verified;
        }
        pin = Remember(address, learned);
    }

    request.options.acceptInvalidCert = true;
    request.options.pinnedPublicKey = pin;
    response = UltraNetResponse();
    UltraNetResult result = UltraNet_HttpRequest(request, response);
    if (result.code == UltraNetResultCode::TlsPublicKeyMismatch) {
        result.message = "The device at " + address +
                         " presented a different certificate from the one it was trusted with "
                         "the first time, so the connection was refused." +
                         ForgetHint(address);
    }
    return result;
}

}  // namespace Internal

}  // namespace UltraCanvas

#endif  // ULTRACANVAS_HAS_NET
