// include/UltraNet/UltraNetCurlMailAuth.h
// The sign-in of a libcurl mail session (IMAP / SMTP / POP3): the credentials
// from UltraNetMailOptions plus CURLOPT_LOGIN_OPTIONS restricting curl to the
// chosen UltraNetMailAuth method. Header-only so each loadable plug-in DSO
// can use it; the three plug-ins differ only in how curl spells a plain
// password for their protocol.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetPlugins.h>

#include <curl/curl.h>

namespace ultranet_curlmailauth {

enum class Protocol { Imap, Smtp, Pop3 };

// curl's login options for `auth` on `protocol`, or nullptr for "whatever the
// server offers". IMAP's normal password is the LOGIN command (curl cannot
// combine it with SASL fallbacks, and every IMAP4rev1 server accepts it);
// POP3's is USER/PASS, which curl only uses with no restriction at all.
inline const char* LoginOptions(UltraNetMailAuth auth, Protocol protocol) {
    switch (auth) {
        case UltraNetMailAuth::Password:
            if (protocol == Protocol::Imap) return "AUTH=+LOGIN";
            if (protocol == Protocol::Smtp) return "AUTH=PLAIN;AUTH=LOGIN";
            return nullptr;
        case UltraNetMailAuth::EncryptedPassword:
            if (protocol == Protocol::Pop3) return "AUTH=+APOP";
            return "AUTH=CRAM-MD5;AUTH=DIGEST-MD5";
        case UltraNetMailAuth::OAuth2:   return "AUTH=XOAUTH2;AUTH=OAUTHBEARER";
        case UltraNetMailAuth::Kerberos: return "AUTH=GSSAPI";
        case UltraNetMailAuth::NTLM:     return "AUTH=NTLM";
        case UltraNetMailAuth::Any:
        case UltraNetMailAuth::None:
        default:                         return nullptr;
    }
}

// Sets the session's credentials and login options on `h`. Fails - before
// anything is sent - when the method and the credentials cannot go together:
// OAuth2 without a token, or a token with a password-type method.
inline UltraNetResult Apply(CURL* h, const UltraNetMailOptions& opt, Protocol protocol) {
    if (opt.auth == UltraNetMailAuth::None) return UltraNetResult::Ok();

    const auto& cred = opt.credentials;
    const bool bearer = !cred.token.empty() &&
        (cred.type == UltraNetAuthType::OAuth2 || cred.type == UltraNetAuthType::Bearer);
    if (opt.auth == UltraNetMailAuth::OAuth2 && !bearer)
        return UltraNetResult::Error(UltraNetResultCode::AuthenticationRequired,
            "OAuth2 is set as the authentication method, but there is no OAuth2 "
            "sign-in for this account");
    if (bearer && opt.auth != UltraNetMailAuth::Any && opt.auth != UltraNetMailAuth::OAuth2)
        return UltraNetResult::Error(UltraNetResultCode::InvalidState,
            "the account signs in with OAuth2, but a different authentication "
            "method is set for this server");

    if (bearer) {
        // XOAUTH2 for Gmail / Microsoft: username + bearer token.
        if (!cred.username.empty())
            curl_easy_setopt(h, CURLOPT_USERNAME, cred.username.c_str());
        curl_easy_setopt(h, CURLOPT_XOAUTH2_BEARER, cred.token.c_str());
    } else if (!cred.username.empty()) {
        curl_easy_setopt(h, CURLOPT_USERNAME, cred.username.c_str());
        curl_easy_setopt(h, CURLOPT_PASSWORD, cred.password.c_str());
    }
    if (const char* login = LoginOptions(opt.auth, protocol))
        curl_easy_setopt(h, CURLOPT_LOGIN_OPTIONS, login);
    return UltraNetResult::Ok();
}

} // namespace ultranet_curlmailauth
