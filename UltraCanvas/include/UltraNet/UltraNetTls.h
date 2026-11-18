// include/UltraNet/UltraNetTls.h
// TLS over an UltraNet TCP handle: UltraNet_TlsWrap a connected TCP handle,
// UltraNet_TlsHandshake it, then read and write through the TCP calls as
// usual. Implemented on every platform - OpenSSL on Linux and Android
// (OS/Linux/UltraNetTlsImpl.cpp), Schannel on Windows
// (OS/MSWindows/UltraNetTlsImpl.cpp), SecureTransport on macOS
// (OS/MacOS/UltraNetTlsImpl.mm) - with peer and host-name verification
// against the system roots, a CA bundle or certificates added at run time,
// and public-key pinning. Use it for protocols on a raw socket (IRC over
// TLS, XMPP's direct TLS, SMTPS ...); HTTPS, WSS and FTPS go through
// UltraNet_Http*, UltraNet_WebSocket* and UltraNet_Ftp*, which carry their
// own TLS.
// Version: 0.4.0
// Last Modified: 2026-11-17
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

// The public-key pin of a certificate: "sha256//" + base64 of the SHA-256
// of its SubjectPublicKeyInfo, which is the form libcurl's
// CURLOPT_PINNEDPUBLICKEY - and UltraNetHttpOptions::pinnedPublicKey - take,
// and what `openssl x509 -pubkey -noout | openssl pkey -pubin -outform der |
// openssl dgst -sha256 -binary | base64` prints. `certificate` is DER, or PEM
// (its first certificate). Empty when it is neither. Pure: no TLS library.
std::string UltraNet_PublicKeyPinOf(const std::vector<uint8_t>& certificate);

UltraNetResult UltraNet_TlsSetCABundle(const std::string& caBundlePath);
UltraNetResult UltraNet_TlsAddTrustedCert(const std::string& certPemData);
