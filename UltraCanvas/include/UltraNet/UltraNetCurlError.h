// include/UltraNet/UltraNetCurlError.h
// curl_easy_perform with the specific failure reason kept. curl_easy_strerror
// only names the error class ("SSL peer certificate or SSH remote key was not
// OK"); the per-transfer error buffer says what actually went wrong ("SSL
// certificate problem: unable to get local issuer certificate", "...
// certificate has expired", "no alternative certificate subject name matches
// target host name ..."). That detail is what tells a user — or a bug report —
// whether the server, the trust store or the network is at fault, and it is
// the only trace a Windows GUI build leaves (it has no stderr for
// ULTRANET_CURL_VERBOSE). Header-only so each loadable plug-in DSO can use it.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <curl/curl.h>

#include <string>

namespace ultranet_curlerror {

// Runs the transfer on `h`; on failure `outMessage` holds curl's specific
// reason, falling back to curl_easy_strerror when curl left none. The error
// buffer is detached again before returning, so a reused handle never points
// at this (stack) buffer.
inline CURLcode Perform(CURL* h, std::string& outMessage) {
    char buf[CURL_ERROR_SIZE];
    buf[0] = '\0';
    curl_easy_setopt(h, CURLOPT_ERRORBUFFER, buf);
    const CURLcode rc = curl_easy_perform(h);
    curl_easy_setopt(h, CURLOPT_ERRORBUFFER, static_cast<char*>(nullptr));
    if (rc == CURLE_OK) {
        outMessage.clear();
        return rc;
    }
    std::string detail(buf);
    while (!detail.empty() && (detail.back() == '\n' || detail.back() == '\r' || detail.back() == ' '))
        detail.pop_back();
    outMessage = detail.empty() ? std::string(curl_easy_strerror(rc)) : detail;
    return rc;
}

} // namespace ultranet_curlerror
