// include/UltraNet/UltraNetTls.h
// TLS over an UltraNet TCP handle. Public surface only — the per-platform
// implementation (OpenSSL / Schannel / SecureTransport) is reserved for
// UltraNet v1.1 because every HTTPS/WSS/FTPS use case is already covered
// by UltraNet_Http*, UltraNet_WebSocket*, and UltraNet_Ftp* (which carry
// their own TLS through libcurl). The Stage-3 stubs return Unknown with
// a clear "deferred" message so callers fail loudly, not silently.
// Version: 0.3.0 (Stage 3)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraNetCore.h"

#include <cstdint>
#include <string>
#include <vector>

struct UltraNetTlsOptions {
    UltraNetTlsVersion minVersion = UltraNetTlsVersion::Tls12;
    UltraNetTlsVersion maxVersion = UltraNetTlsVersion::Auto;
    bool verifyPeer = true;
    bool verifyHostname = true;
    std::string caBundlePath;
    std::vector<uint8_t> clientCertPem;
    std::vector<uint8_t> clientKeyPem;
    std::string clientKeyPassword;
    std::vector<std::string> alpnProtocols;
    std::string sniHostname;

    static UltraNetTlsOptions Default() { return {}; }
};

struct UltraNetCertificate {
    std::string subject;
    std::string issuer;
    std::string serialNumber;
    std::string fingerprintSha256;
    std::string notBefore;
    std::string notAfter;
    std::vector<std::string> subjectAltNames;
    std::vector<uint8_t> derEncoded;
    bool isSelfSigned = false;
    bool isCa = false;
};

UltraNetHandle UltraNet_TlsWrap(
    UltraNetHandle tcpHandle,
    const std::string& serverName,
    const UltraNetTlsOptions& options = UltraNetTlsOptions::Default());

UltraNetResult UltraNet_TlsHandshake(UltraNetHandle handle);
UltraNetTlsInfo UltraNet_TlsGetInfo(UltraNetHandle handle);

UltraNetResult UltraNet_TlsSetCABundle(const std::string& caBundlePath);
UltraNetResult UltraNet_TlsAddTrustedCert(const std::string& certPemData);
