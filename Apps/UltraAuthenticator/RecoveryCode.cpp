// Apps/UltraAuthenticator/RecoveryCode.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "RecoveryCode.h"

#include "UltraCanvasTextUtils.h"
#include "store/EncryptedFileStore.h"

#include <cctype>
#include <vector>

namespace UltraCanvas {
namespace Authenticator {

std::string FormatRecoveryCode(const UltraCryptSecureBuffer& share) {
    if (share.GetSize() != kRecoveryShareBytes) return {};
    std::string plain = UltraCanvas::Base32Encode(share.Data(), share.GetSize(), false);
    std::string out;
    out.reserve(plain.size() + plain.size() / 4);
    for (size_t i = 0; i < plain.size(); ++i) {
        if (i > 0 && i % 4 == 0) out.push_back('-');
        out.push_back(plain[i]);
    }
    UltraCrypt_SecureZero(plain.data(), plain.size());
    return out;
}

bool ParseRecoveryCode(const std::string& text, UltraCryptSecureBuffer& outShare) {
    outShare.Clear();
    std::string cleaned;
    cleaned.reserve(text.size());
    for (char c : text) {
        if (c == '-' || c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        cleaned.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    UltraCryptSecureBuffer decoded;
    const bool ok = static_cast<bool>(UltraCrypt_Base32Decode(cleaned, decoded)) &&
                    decoded.GetSize() == kRecoveryShareBytes;
    UltraCrypt_SecureZero(cleaned.data(), cleaned.size());
    if (!ok) return false;
    outShare = std::move(decoded);
    return true;
}

std::string ShareToHex(const UltraCryptSecureBuffer& share) {
    static const char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(share.GetSize() * 2);
    for (size_t i = 0; i < share.GetSize(); ++i) {
        out.push_back(kDigits[share.Data()[i] >> 4]);
        out.push_back(kDigits[share.Data()[i] & 0x0F]);
    }
    return out;
}

bool ShareFromHex(const std::string& hex, UltraCryptSecureBuffer& outShare) {
    outShare.Clear();
    if (hex.size() != kRecoveryShareBytes * 2) return false;
    UltraCryptSecureBuffer bytes(kRecoveryShareBytes);
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < kRecoveryShareBytes; ++i) {
        const int high = nibble(hex[2 * i]);
        const int low = nibble(hex[2 * i + 1]);
        if (high < 0 || low < 0) return false;
        bytes.Data()[i] = static_cast<uint8_t>((high << 4) | low);
    }
    outShare = std::move(bytes);
    return true;
}

} // namespace Authenticator
} // namespace UltraCanvas
