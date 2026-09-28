// include/UltraNet/UltraNetCurlTls.h
// CURLOPT_SSL_OPTIONS for every libcurl handle UltraNet opens - the HTTP
// client and the IMAP / SMTP / POP3 plug-ins - so they verify certificates the
// same way. Header-only so each loadable plug-in DSO can use it.
//
// Windows only (0 elsewhere):
//   NATIVE_CA          trust the Windows certificate store (an OpenSSL build
//                      reads it on top of any CA file; Schannel uses it anyway
//                      when no CA file is given - UltraNet_ResolveCaBundlePath
//                      gives none under Schannel).
//   REVOKE_BEST_EFFORT Schannel checks revocation and by default FAILS when
//                      the CRL / OCSP server cannot be reached (a captive
//                      network, a firewall, an offline CA endpoint) with
//                      CRYPT_E_REVOCATION_OFFLINE. Best effort still refuses
//                      a certificate that IS revoked - the check browsers make.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <curl/curl.h>

namespace ultranet_curltls {

inline long SslOptions() {
    long opts = 0;
#if defined(_WIN32)
#if defined(CURLSSLOPT_NATIVE_CA)
    opts |= CURLSSLOPT_NATIVE_CA;
#endif
#if defined(CURLSSLOPT_REVOKE_BEST_EFFORT)
    opts |= CURLSSLOPT_REVOKE_BEST_EFFORT;
#endif
#endif
    return opts;
}

// Sets them on `h` when there are any (a no-op off Windows).
inline void Apply(CURL* h) {
    if (const long opts = SslOptions())
        curl_easy_setopt(h, CURLOPT_SSL_OPTIONS, opts);
}

} // namespace ultranet_curltls
