// Apps/UltraAuthenticator/otp/UltraOtp.cpp
// HOTP / TOTP implementation. See the header for design notes.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraOtp.h"

#include <cstring>
#include <strings.h>

namespace UltraCanvas {
namespace Otp {
namespace {

// 10^digits for digits in [6, 8]. A table rather than pow() so the modulus is
// exact — floating-point rounding here would silently corrupt codes.
uint32_t PowerOfTen(uint32_t digits) {
    switch (digits) {
        case 6: return 1000000u;
        case 7: return 10000000u;
        case 8: return 100000000u;
    }
    return 0;
}

UltraCryptHashAlgorithm ToHashAlgorithm(Algorithm algorithm) {
    switch (algorithm) {
        case Algorithm::SHA1:   return UltraCryptHashAlgorithm::SHA1;
        case Algorithm::SHA256: return UltraCryptHashAlgorithm::SHA256;
        case Algorithm::SHA512: return UltraCryptHashAlgorithm::SHA512;
    }
    return UltraCryptHashAlgorithm::SHA1;
}

bool EqualsIgnoreCase(const std::string& a, const char* b) {
    return strcasecmp(a.c_str(), b) == 0;
}

// Left-pads with '0' to exactly `digits` characters. RFC 4226 §5.3 requires
// the leading zeros: "123456" and "023456" are different codes.
std::string FormatCode(uint32_t value, uint32_t digits) {
    std::string text = std::to_string(value);
    if (text.size() < digits) text.insert(0, digits - text.size(), '0');
    return text;
}

} // namespace

Result Result::Ok() {
    Result r;
    r.success = true;
    return r;
}

Result Result::Error(const std::string& message) {
    Result r;
    r.success = false;
    r.message = message;
    return r;
}

Result ValidateParameters(const Parameters& params) {
    if (params.digits < kMinDigits || params.digits > kMaxDigits) {
        return Result::Error("digits must be between 6 and 8");
    }
    if (params.type == Type::Totp) {
        if (params.periodSeconds < kMinPeriodSeconds ||
            params.periodSeconds > kMaxPeriodSeconds) {
            return Result::Error("period must be between 15 and 120 seconds");
        }
    }
    switch (params.algorithm) {
        case Algorithm::SHA1:
        case Algorithm::SHA256:
        case Algorithm::SHA512:
            break;
        default:
            return Result::Error("unknown OTP algorithm");
    }
    return Result::Ok();
}

Result ValidateSecret(const UltraCryptSecureBuffer& secret) {
    if (secret.GetSize() < kMinSecretBytes) {
        // RFC 4226 §4 R6. Short secrets are the classic sign of a malformed or
        // hand-made provisioning URI.
        return Result::Error("shared secret is shorter than the 128-bit minimum");
    }
    if (secret.GetSize() > kMaxSecretBytes) {
        return Result::Error("shared secret is implausibly long");
    }
    return Result::Ok();
}

Result GenerateHotp(const UltraCryptSecureBuffer& secret,
                    const Parameters& params,
                    uint64_t counter,
                    std::string& outCode) {
    outCode.clear();

    auto paramCheck = ValidateParameters(params);
    if (!paramCheck) return paramCheck;
    auto secretCheck = ValidateSecret(secret);
    if (!secretCheck) return secretCheck;

    if (!UltraCrypt_IsAvailable()) {
        return Result::Error("no cryptography backend is available, so codes "
                             "cannot be generated");
    }

    // RFC 4226 §5.1: the moving factor is an 8-byte big-endian counter.
    uint8_t message[8];
    for (int i = 0; i < 8; ++i) {
        message[7 - i] = static_cast<uint8_t>((counter >> (i * 8)) & 0xFF);
    }

    std::vector<uint8_t> mac;
    auto hmac = UltraCrypt_Hmac(ToHashAlgorithm(params.algorithm), secret,
                                message, sizeof(message), mac);
    UltraCrypt_SecureZero(message, sizeof(message));
    if (!hmac) {
        return Result::Error("HMAC computation failed: " + hmac.message);
    }
    if (mac.size() < 20) {
        return Result::Error("HMAC output was unexpectedly short");
    }

    // RFC 4226 §5.3 dynamic truncation. The low nibble of the last byte picks
    // the offset; the high bit of the first selected byte is masked off so the
    // result is a positive 31-bit integer on every platform.
    const size_t offset = static_cast<size_t>(mac[mac.size() - 1] & 0x0F);
    const uint32_t binary =
        (static_cast<uint32_t>(mac[offset]     & 0x7F) << 24) |
        (static_cast<uint32_t>(mac[offset + 1] & 0xFF) << 16) |
        (static_cast<uint32_t>(mac[offset + 2] & 0xFF) <<  8) |
        (static_cast<uint32_t>(mac[offset + 3] & 0xFF));

    const uint32_t modulus = PowerOfTen(params.digits);
    if (modulus == 0) {
        return Result::Error("unsupported digit count");
    }
    outCode = FormatCode(binary % modulus, params.digits);

    UltraCrypt_SecureZero(mac.data(), mac.size());
    return Result::Ok();
}

Result GenerateTotp(const UltraCryptSecureBuffer& secret,
                    const Parameters& params,
                    int64_t unixTimeUtc,
                    std::string& outCode) {
    outCode.clear();

    auto paramCheck = ValidateParameters(params);
    if (!paramCheck) return paramCheck;

    uint64_t counter = 0;
    if (!TimeStepCounter(unixTimeUtc, params.periodSeconds, counter)) {
        return Result::Error("invalid time or period for a time-based code");
    }

    // TOTP is HOTP with the time step as the moving factor (RFC 6238 §4).
    Parameters hotpParams = params;
    hotpParams.type = Type::Hotp;
    return GenerateHotp(secret, hotpParams, counter, outCode);
}

bool TimeStepCounter(int64_t unixTimeUtc, uint32_t periodSeconds,
                     uint64_t& outCounter) {
    if (periodSeconds == 0) return false;
    if (unixTimeUtc < 0) return false;   // before the epoch: reject, never wrap
    outCounter = static_cast<uint64_t>(unixTimeUtc) /
                 static_cast<uint64_t>(periodSeconds);
    return true;
}

uint32_t SecondsRemaining(int64_t unixTimeUtc, uint32_t periodSeconds) {
    if (periodSeconds == 0 || unixTimeUtc < 0) return 0;
    const uint64_t elapsed = static_cast<uint64_t>(unixTimeUtc) %
                             static_cast<uint64_t>(periodSeconds);
    return static_cast<uint32_t>(periodSeconds - elapsed);
}

const char* AlgorithmName(Algorithm algorithm) {
    switch (algorithm) {
        case Algorithm::SHA1:   return "SHA1";
        case Algorithm::SHA256: return "SHA256";
        case Algorithm::SHA512: return "SHA512";
    }
    return "SHA1";
}

bool ParseAlgorithmName(const std::string& text, Algorithm& outAlgorithm) {
    if (EqualsIgnoreCase(text, "SHA1"))   { outAlgorithm = Algorithm::SHA1;   return true; }
    if (EqualsIgnoreCase(text, "SHA256")) { outAlgorithm = Algorithm::SHA256; return true; }
    if (EqualsIgnoreCase(text, "SHA512")) { outAlgorithm = Algorithm::SHA512; return true; }
    return false;
}

const char* TypeName(Type type) {
    return type == Type::Hotp ? "hotp" : "totp";
}

bool ParseTypeName(const std::string& text, Type& outType) {
    if (EqualsIgnoreCase(text, "totp")) { outType = Type::Totp; return true; }
    if (EqualsIgnoreCase(text, "hotp")) { outType = Type::Hotp; return true; }
    return false;
}

} // namespace Otp
} // namespace UltraCanvas
