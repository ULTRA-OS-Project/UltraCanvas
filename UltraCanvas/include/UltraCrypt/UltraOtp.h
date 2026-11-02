// include/UltraCrypt/UltraOtp.h
// HOTP (RFC 4226) and TOTP (RFC 6238) one-time passwords: generated (an
// authenticator - UltraAuthenticator) and checked (a sign-in that asks for
// one - ChatCube's "authenticator code").
//
// Protocol-level construction built on UltraCrypt's primitives, per the scope
// split in Docs/Modules/UltraCrypt/README.md §2: UltraCrypt owns the
// primitives, consumers own the protocols. This unit depends only on
// UltraCrypt and the standard library — no UI, no storage, no document model —
// so it compiles and is unit-tested on its own
// (Tests/UltraOtpTests.cpp, against the RFC 4226 App. D and RFC 6238 App. B
// vectors). It sat in Apps/UltraAuthenticator/otp until a second program
// needed it; it is part of the UltraCrypt module now.
//
// Design notes:
//  - Secrets travel in UltraCryptSecureBuffer, never std::string.
//  - Every externally supplied value is bounds-checked (see the k* constants);
//    an authenticator's parameters come from a scanned QR code, which is
//    attacker-controlled input.
//  - Time is UTC seconds since the Unix epoch. There is no local-time path and
//    no clock-skew compensation: skew produces wrong codes, which is a
//    usability problem to surface, not a correctness knob to widen.
//  - VerifyTotp is the checking side, for a program that asks for a code:
//    a window of a time step either side (RFC 6238 §5.2's "one step
//    backward"), single use per time step (the caller keeps the last step
//    accepted and hands it back) and a constant-time comparison of every
//    candidate, so neither the timing nor the step that matched leaks.
//
// Version: 0.2.0 - in UltraCrypt (from Apps/UltraAuthenticator/otp); VerifyTotp, NewTotpSecret
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once
#ifndef ULTRAOTP_H
#define ULTRAOTP_H

#include "UltraCrypt/UltraCryptCore.h"

#include <cstdint>
#include <string>

namespace UltraCanvas {
namespace Otp {

enum class Algorithm : uint8_t {
    SHA1,     // RFC 6238 default; what most issuers emit
    SHA256,
    SHA512
};

enum class Type : uint8_t {
    Totp,     // RFC 6238, time-based
    Hotp      // RFC 4226, counter-based
};

// Accepted ranges. Anything outside these is rejected rather than clamped, so
// a hostile or malformed provisioning URI cannot quietly produce a weaker
// configuration than it appears to.
constexpr uint32_t kMinDigits         = 6;
constexpr uint32_t kMaxDigits         = 8;
constexpr uint32_t kDefaultDigits     = 6;
constexpr uint32_t kMinPeriodSeconds  = 15;
constexpr uint32_t kMaxPeriodSeconds  = 120;
constexpr uint32_t kDefaultPeriod     = 30;

// RFC 4226 §4 R6 requires at least 128 bits of shared secret. The upper bound
// is a sanity limit on untrusted input, well above any real issuer's key.
constexpr size_t   kMinSecretBytes    = 16;
constexpr size_t   kMaxSecretBytes    = 1024;

struct Parameters {
    Type      type          = Type::Totp;
    Algorithm algorithm     = Algorithm::SHA1;
    uint32_t  digits        = kDefaultDigits;
    uint32_t  periodSeconds = kDefaultPeriod;   // TOTP only
    uint64_t  counter       = 0;                // HOTP only

    // Display metadata; not an input to code generation.
    std::string issuer;
    std::string accountName;
};

struct Result {
    bool success = false;
    std::string message;    // developer-facing; never contains secret material

    operator bool() const { return success; }

    static Result Ok();
    static Result Error(const std::string& message);
};

// Checks digits / period / type-specific fields against the bounds above.
// Does not inspect the secret — see ValidateSecret.
Result ValidateParameters(const Parameters& params);

// Checks the secret length against kMinSecretBytes / kMaxSecretBytes.
Result ValidateSecret(const UltraCryptSecureBuffer& secret);

// RFC 4226. `counter` is the moving factor and overrides params.counter, so a
// caller can generate a window without mutating its stored parameters.
//
// Note for callers persisting HOTP state: write the incremented counter to
// durable storage *before* showing the code. A crash between display and save
// otherwise replays a counter, and a replayed HOTP code is a reused code.
Result GenerateHotp(const UltraCryptSecureBuffer& secret,
                    const Parameters& params,
                    uint64_t counter,
                    std::string& outCode);

// RFC 6238. `unixTimeUtc` is seconds since the Unix epoch, UTC. Times before
// the epoch are rejected rather than wrapped.
Result GenerateTotp(const UltraCryptSecureBuffer& secret,
                    const Parameters& params,
                    int64_t unixTimeUtc,
                    std::string& outCode);

// The checking side, for a sign-in that asks for an authenticator code.
// Accepts `code` when it is the code of the time step at `unixTimeUtc` or of
// up to `windowSteps` steps either side of it (1: the clock of the phone may
// be up to one period off), and that step is later than `lastAcceptedStep`
// (a code is good once: pass the step a previous call accepted, 0 for
// none). On success `outAcceptedStep` is the step that matched - keep it and
// pass it next time. Every candidate is compared in constant time, and all
// of them are computed, whichever matched. `code` may hold spaces ("123
// 456"); anything else but its digits fails. False, with a message, when it
// is not one of the codes - the message never says which part was wrong.
constexpr uint32_t kMaxVerifyWindowSteps = 2;
Result VerifyTotp(const UltraCryptSecureBuffer& secret,
                  const Parameters& params,
                  int64_t unixTimeUtc,
                  const std::string& code,
                  uint32_t windowSteps,
                  uint64_t lastAcceptedStep,
                  uint64_t& outAcceptedStep);

// A new random shared secret of `bytes` bytes (20 - 160 bits - is what RFC
// 4226 recommends and what every authenticator app takes), for a sign-in
// that sets up an authenticator. False without a crypto backend.
Result NewTotpSecret(UltraCryptSecureBuffer& outSecret, size_t bytes = 20);

// floor(unixTimeUtc / periodSeconds) — the RFC 6238 time-step counter T.
// Returns false for a negative time or a zero period.
bool TimeStepCounter(int64_t unixTimeUtc, uint32_t periodSeconds,
                     uint64_t& outCounter);

// Seconds until the currently displayed code changes, in [1, periodSeconds].
// Returns 0 for invalid input, which callers should treat as "unknown".
uint32_t SecondsRemaining(int64_t unixTimeUtc, uint32_t periodSeconds);

// "SHA1" / "SHA256" / "SHA512", as spelled in the otpauth:// key URI format.
const char* AlgorithmName(Algorithm algorithm);

// Case-insensitive; accepts exactly the three names above.
bool ParseAlgorithmName(const std::string& text, Algorithm& outAlgorithm);

const char* TypeName(Type type);                                  // "totp"/"hotp"
bool ParseTypeName(const std::string& text, Type& outType);       // case-insensitive

} // namespace Otp
} // namespace UltraCanvas

#endif // ULTRAOTP_H
