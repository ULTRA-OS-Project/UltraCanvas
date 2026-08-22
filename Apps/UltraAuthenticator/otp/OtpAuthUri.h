// Apps/UltraAuthenticator/otp/OtpAuthUri.h
// Parsing and building of the de-facto `otpauth://` Key URI format.
//
// This is the app's primary untrusted-input boundary: a provisioning URI
// normally arrives from a QR code, which means from whatever was pointed at
// the camera. Everything here validates strictly and fails closed —
// see Docs/UltraAuthenticator/UltraAuthenticator-Investigation.md §3.3.
//
// Format:  otpauth://TYPE/LABEL?PARAMETERS
//   TYPE       totp | hotp
//   LABEL      "Issuer:Account" or "Account" (percent-encoded)
//   PARAMETERS secret (required, Base32), issuer, algorithm, digits,
//              period (totp), counter (required for hotp)
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once
#ifndef OTPAUTHURI_H
#define OTPAUTHURI_H

#include "UltraOtp.h"

#include <string>

namespace UltraCanvas {
namespace Otp {

// A provisioning URI is small; anything larger is malformed or hostile.
constexpr size_t kMaxUriLength   = 4096;
// Bounds on the human-readable fields, so a hostile label cannot flood the UI.
constexpr size_t kMaxLabelLength = 256;

// Parses and strictly validates a provisioning URI.
//
// On success `outParams` holds validated parameters and `outSecret` the decoded
// shared secret. On failure both are left empty and `message` says why.
//
// Rejected, among others: any scheme other than otpauth, any type other than
// totp/hotp (including otpauth-migration, which is a different format
// entirely), a missing or non-Base32 secret, a secret outside the length
// bounds, digits/period/counter outside their ranges, an hotp URI with no
// counter, control characters in the label, and an issuer path prefix that
// disagrees with the issuer parameter.
//
// Unknown query parameters are ignored rather than rejected, so that a URI
// carrying a future extension still provisions correctly.
Result ParseOtpAuthUri(const std::string& uri,
                       Parameters& outParams,
                       UltraCryptSecureBuffer& outSecret);

// Builds a provisioning URI from validated parameters.
//
// WARNING: the returned string contains the Base32-encoded shared secret. It
// is exactly as sensitive as the secret itself — do not log it, and clear it
// as soon as the transfer UI closes. Displaying it as a QR code puts the
// secret on screen, which on X11 is readable by any process of the same user.
Result BuildOtpAuthUri(const Parameters& params,
                       const UltraCryptSecureBuffer& secret,
                       std::string& outUri);

} // namespace Otp
} // namespace UltraCanvas

#endif // OTPAUTHURI_H
