// Tests/UltraNet/ApiStatus/ProbeTls.cpp
// Probes for UltraNet/UltraNetTls.h — wrapping a TCP handle in TLS, the
// handshake, session introspection, and the global trust store.
//
// Verifying TLS needs a TLS peer. Rather than reaching out to the internet
// (which would make the report non-deterministic and CI-hostile), the probes
// spawn `openssl s_server` on loopback with a throwaway self-signed
// certificate. That also makes the trust-store entries checkable in both
// directions: the same handshake must FAIL against the default trust store and
// SUCCEED once the probe's own certificate is trusted.
//
// Where the openssl CLI is unavailable (or on Windows, where the probe does
// not spawn subprocesses), the entries report IMPLEMENTED rather than
// pretending the surface is unverifiable in principle.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "ApiStatus.h"

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetSocket.h>
#include <UltraNet/UltraNetTls.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <csignal>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace ultranet_apistatus;

namespace {

constexpr const char* kArea = "TLS";
constexpr int kTlsPort = 18890;

std::string ReadWholeFile(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream os;
    os << in.rdbuf();
    return os.str();
}

// `openssl s_server` on loopback with a self-signed certificate for
// CN=localhost (SAN localhost + 127.0.0.1). Started once, torn down when the
// process exits.
class TlsPeer {
public:
    static TlsPeer& Instance() {
        static TlsPeer p;
        return p;
    }

    // Returns true when a peer is listening. `outWhyNot` explains a false.
    bool Start(std::string& outWhyNot) {
        if (started_) return true;
        if (!whyNot_.empty()) { outWhyNot = whyNot_; return false; }
#if defined(_WIN32) || defined(_WIN64)
        whyNot_ = "the TLS probe peer (openssl s_server) is only spawned on "
                  "POSIX hosts";
        outWhyNot = whyNot_;
        return false;
#else
        if (std::system("command -v openssl > /dev/null 2>&1") != 0) {
            whyNot_ = "the openssl command-line tool is not on PATH, so no TLS "
                      "peer could be started";
            outWhyNot = whyNot_;
            return false;
        }

        dir_ = std::filesystem::temp_directory_path() / "ultranet_apistatus_tls";
        std::error_code ec;
        std::filesystem::create_directories(dir_, ec);
        certPath_ = (dir_ / "cert.pem").string();
        keyPath_  = (dir_ / "key.pem").string();

        const std::string gen =
            "openssl req -x509 -newkey rsa:2048 -nodes -days 1 "
            "-subj \"/CN=localhost\" "
            "-addext \"subjectAltName=DNS:localhost,IP:127.0.0.1\" "
            "-keyout \"" + keyPath_ + "\" -out \"" + certPath_ + "\" "
            "> /dev/null 2>&1";
        if (std::system(gen.c_str()) != 0 ||
            !std::filesystem::exists(certPath_)) {
            whyNot_ = "openssl could not generate a self-signed probe certificate";
            outWhyNot = whyNot_;
            return false;
        }
        certPem_ = ReadWholeFile(certPath_);

        const pid_t pid = ::fork();
        if (pid < 0) {
            whyNot_ = "fork() failed while starting the TLS probe peer";
            outWhyNot = whyNot_;
            return false;
        }
        if (pid == 0) {
            // Silence the server; the redirect failing is not worth acting on
            // in a child that is about to exec.
            if (!std::freopen("/dev/null", "w", stdout)) { /* ignored */ }
            if (!std::freopen("/dev/null", "w", stderr)) { /* ignored */ }
            if (!std::freopen("/dev/null", "r", stdin))  { /* ignored */ }
            const std::string port = std::to_string(kTlsPort);
            ::execlp("openssl", "openssl", "s_server",
                     "-accept", port.c_str(),
                     "-cert", certPath_.c_str(),
                     "-key",  keyPath_.c_str(),
                     "-www",
                     static_cast<char*>(nullptr));
            ::_exit(127);
        }
        pid_ = pid;

        // Wait for the listener to come up.
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline) {
            UltraNetSocketOptions opt;
            opt.connectTimeoutMs = 200;
            UltraNetHandle h = UltraNet_TcpConnect("127.0.0.1", kTlsPort, opt);
            if (h != UltraNetInvalidHandle) {
                UltraNet_SocketClose(h);
                started_ = true;
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        Stop();
        whyNot_ = "openssl s_server did not start listening within 5s";
        outWhyNot = whyNot_;
        return false;
#endif
    }

    void Stop() {
#if !defined(_WIN32)
        if (pid_ > 0) {
            ::kill(pid_, SIGTERM);
            int status = 0;
            ::waitpid(pid_, &status, 0);
            pid_ = -1;
        }
        if (!dir_.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(dir_, ec);
        }
        started_ = false;
#endif
    }

    ~TlsPeer() { Stop(); }

    int Port() const { return kTlsPort; }
    const std::string& CertPath() const { return certPath_; }
    const std::string& CertPem()  const { return certPem_; }

private:
    TlsPeer() = default;
    bool        started_ = false;
    std::string whyNot_;
    std::filesystem::path dir_;
    std::string certPath_;
    std::string keyPath_;
    std::string certPem_;
#if !defined(_WIN32)
    pid_t pid_ = -1;
#endif
};

// Opens a TCP connection to the probe peer. Returns the invalid handle on
// failure.
UltraNetHandle ConnectToPeer() {
    UltraNetSocketOptions opt;
    opt.connectTimeoutMs = 3000;
    return UltraNet_TcpConnect("127.0.0.1", TlsPeer::Instance().Port(), opt);
}

// Closes whatever the probe opened, however the probe returns.
struct SocketGuard {
    UltraNetHandle handle = UltraNetInvalidHandle;
    ~SocketGuard() { if (handle != UltraNetInvalidHandle) UltraNet_SocketClose(handle); }
};

} // namespace

ULTRANET_PROBE(kArea, UltraNet_TlsWrap) {
    // A handle that is not an open TCP stream can never be wrapped.
    PROBE_EXPECT(UltraNet_TlsWrap(UltraNetInvalidHandle, "localhost") ==
                 UltraNetInvalidHandle);

    std::string whyNot;
    if (!TlsPeer::Instance().Start(whyNot)) {
        return Implemented("rejects non-TCP handles, but the wrap itself is "
                           "unverified: " + whyNot);
    }

    SocketGuard sock{ConnectToPeer()};
    PROBE_EXPECT_MSG(sock.handle != UltraNetInvalidHandle,
                     "could not reach the loopback TLS peer");

    UltraNetTlsOptions opt;
    opt.verifyPeer = false;          // self-signed; trust is probed separately
    opt.verifyHostname = false;
    const UltraNetHandle wrapped = UltraNet_TlsWrap(sock.handle, "localhost", opt);
    PROBE_EXPECT(wrapped == sock.handle);   // TLS layers onto the same handle
    return Working("layered TLS onto a live TCP handle; non-TCP handles rejected");
}

ULTRANET_PROBE(kArea, UltraNet_TlsHandshake) {
    // No TLS attached => InvalidHandle, not a crash.
    const UltraNetResult unattached = UltraNet_TlsHandshake(UltraNetInvalidHandle);
    PROBE_EXPECT(!unattached && unattached.code == UltraNetResultCode::InvalidHandle);

    std::string whyNot;
    if (!TlsPeer::Instance().Start(whyNot)) {
        return Implemented("unattached handles rejected, but the handshake is "
                           "unverified: " + whyNot);
    }

    SocketGuard sock{ConnectToPeer()};
    PROBE_EXPECT(sock.handle != UltraNetInvalidHandle);

    UltraNetTlsOptions opt;
    opt.verifyPeer = false;
    opt.verifyHostname = false;
    PROBE_EXPECT(UltraNet_TlsWrap(sock.handle, "localhost", opt) == sock.handle);

    const UltraNetResult r = UltraNet_TlsHandshake(sock.handle);
    PROBE_EXPECT_MSG(static_cast<bool>(r), r.message);

    // Application data must now flow through the TLS layer transparently:
    // s_server -www answers a plain HTTP request over the secure channel.
    const std::string request = "GET / HTTP/1.0\r\n\r\n";
    int sent = 0;
    PROBE_EXPECT(static_cast<bool>(UltraNet_TcpSend(
        sock.handle, std::vector<uint8_t>(request.begin(), request.end()), sent)));

    std::string reply;
    for (int i = 0; i < 20 && reply.size() < 16; ++i) {
        std::vector<uint8_t> chunk;
        if (!UltraNet_TcpReceive(sock.handle, chunk, 65536, 2000)) break;
        if (chunk.empty()) break;
        reply.append(chunk.begin(), chunk.end());
    }
    PROBE_EXPECT_MSG(reply.rfind("HTTP/", 0) == 0,
                     "expected an HTTP reply over TLS, got \"" +
                     reply.substr(0, 40) + "\"");
    return Working("handshake completed and application data flowed through "
                   "the TLS layer (send/receive routed via the TLS vtable)");
}

ULTRANET_PROBE(kArea, UltraNet_TlsGetInfo) {
    const UltraNetTlsInfo none = UltraNet_TlsGetInfo(UltraNetInvalidHandle);
    PROBE_EXPECT(none.cipherSuite.empty());

    std::string whyNot;
    if (!TlsPeer::Instance().Start(whyNot)) {
        return Implemented("returns an empty struct for unknown handles, but "
                           "live session details are unverified: " + whyNot);
    }

    SocketGuard sock{ConnectToPeer()};
    PROBE_EXPECT(sock.handle != UltraNetInvalidHandle);

    UltraNetTlsOptions opt;
    opt.verifyPeer = false;
    opt.verifyHostname = false;
    PROBE_EXPECT(UltraNet_TlsWrap(sock.handle, "localhost", opt) == sock.handle);
    const UltraNetResult handshake = UltraNet_TlsHandshake(sock.handle);
    PROBE_EXPECT_MSG(static_cast<bool>(handshake), handshake.message);

    const UltraNetTlsInfo info = UltraNet_TlsGetInfo(sock.handle);
    PROBE_EXPECT_MSG(!info.cipherSuite.empty(), "cipherSuite came back empty");
    PROBE_EXPECT(info.version == UltraNetTlsVersion::Tls12 ||
                 info.version == UltraNetTlsVersion::Tls13);
    PROBE_EXPECT_MSG(info.peerCertificateSubject.find("localhost") != std::string::npos,
                     "peer subject reported as \"" + info.peerCertificateSubject + "\"");
    return Working("negotiated cipher \"" + info.cipherSuite +
                   "\", peer subject \"" + info.peerCertificateSubject + "\"");
}

ULTRANET_PROBE(kArea, UltraNet_TlsSetCABundle) {
    std::string whyNot;
    if (!TlsPeer::Instance().Start(whyNot)) {
        const UltraNetResult r = UltraNet_TlsSetCABundle("/nonexistent/ca.pem");
        PROBE_EXPECT(static_cast<bool>(r));
        UltraNet_TlsSetCABundle("");
        return Implemented("accepts a bundle path, but its effect on "
                           "verification is unverified: " + whyNot);
    }

    UltraNetTlsOptions verifying;
    verifying.verifyPeer = true;
    verifying.verifyHostname = true;

    // 1. Default trust store: the self-signed peer must be rejected.
    UltraNet_TlsSetCABundle("");
    bool rejectedByDefault = false;
    {
        SocketGuard sock{ConnectToPeer()};
        PROBE_EXPECT(sock.handle != UltraNetInvalidHandle);
        if (UltraNet_TlsWrap(sock.handle, "localhost", verifying) == sock.handle) {
            rejectedByDefault = !UltraNet_TlsHandshake(sock.handle);
        } else {
            rejectedByDefault = true;
        }
    }

    // 2. Same handshake, with the probe's certificate as the CA bundle.
    UltraNetResult trusted = UltraNetResult::Error(UltraNetResultCode::Unknown,
                                                   "not attempted");
    const UltraNetResult set =
        UltraNet_TlsSetCABundle(TlsPeer::Instance().CertPath());
    {
        SocketGuard sock{ConnectToPeer()};
        if (sock.handle != UltraNetInvalidHandle &&
            UltraNet_TlsWrap(sock.handle, "localhost", verifying) == sock.handle) {
            trusted = UltraNet_TlsHandshake(sock.handle);
        }
    }
    UltraNet_TlsSetCABundle("");   // leave the global state as we found it

    PROBE_EXPECT(static_cast<bool>(set));
    PROBE_EXPECT_MSG(rejectedByDefault,
                     "a self-signed peer verified against the default trust store");
    PROBE_EXPECT_MSG(static_cast<bool>(trusted),
                     "handshake still failed after trusting the peer's own "
                     "certificate: " + trusted.message);
    return Working("verification flipped from failure to success purely by "
                   "pointing the CA bundle at the peer's certificate");
}

ULTRANET_PROBE(kArea, UltraNet_TlsAddTrustedCert) {
    std::string whyNot;
    if (!TlsPeer::Instance().Start(whyNot)) {
        const UltraNetResult r = UltraNet_TlsAddTrustedCert("");
        PROBE_EXPECT(static_cast<bool>(r));
        return Implemented("accepts PEM input, but its effect on verification "
                           "is unverified: " + whyNot);
    }

    UltraNetTlsOptions verifying;
    verifying.verifyPeer = true;
    verifying.verifyHostname = true;

    UltraNet_TlsSetCABundle("");
    bool rejectedBefore = false;
    {
        SocketGuard sock{ConnectToPeer()};
        PROBE_EXPECT(sock.handle != UltraNetInvalidHandle);
        if (UltraNet_TlsWrap(sock.handle, "localhost", verifying) == sock.handle) {
            rejectedBefore = !UltraNet_TlsHandshake(sock.handle);
        } else {
            rejectedBefore = true;
        }
    }

    const UltraNetResult added =
        UltraNet_TlsAddTrustedCert(TlsPeer::Instance().CertPem());
    PROBE_EXPECT(static_cast<bool>(added));

    UltraNetResult afterTrust = UltraNetResult::Error(UltraNetResultCode::Unknown,
                                                      "not attempted");
    {
        SocketGuard sock{ConnectToPeer()};
        if (sock.handle != UltraNetInvalidHandle &&
            UltraNet_TlsWrap(sock.handle, "localhost", verifying) == sock.handle) {
            afterTrust = UltraNet_TlsHandshake(sock.handle);
        }
    }

    PROBE_EXPECT_MSG(rejectedBefore,
                     "a self-signed peer verified before its certificate was added");
    PROBE_EXPECT_MSG(static_cast<bool>(afterTrust),
                     "handshake still failed after adding the certificate: " +
                     afterTrust.message);
    return Working("an in-memory PEM added to the global trust store made a "
                   "previously rejected peer verify");
}
