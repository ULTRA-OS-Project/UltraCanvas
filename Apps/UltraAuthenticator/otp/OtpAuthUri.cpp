// Apps/UltraAuthenticator/otp/OtpAuthUri.cpp
// otpauth:// Key URI parsing and building. See the header for the threat note.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "OtpAuthUri.h"

#include "UltraCanvasTextUtils.h"

#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <vector>

namespace UltraCanvas {
namespace Otp {
namespace {

bool EqualsIgnoreCase(const std::string& a, const char* b) {
    return strcasecmp(a.c_str(), b) == 0;
}

bool StartsWithIgnoreCase(const std::string& text, const char* prefix) {
    const size_t n = std::strlen(prefix);
    return text.size() >= n && strncasecmp(text.c_str(), prefix, n) == 0;
}

int HexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Percent-decoding. '+' is left literal rather than turned into a space: the
// Key URI format is a URI, not an HTML form body, and issuers do put '+' in
// account names (notably in e-mail addresses).
bool PercentDecode(const std::string& in, std::string& out) {
    out.clear();
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] != '%') { out.push_back(in[i]); continue; }
        if (i + 2 >= in.size()) return false;
        const int hi = HexValue(in[i + 1]);
        const int lo = HexValue(in[i + 2]);
        if (hi < 0 || lo < 0) return false;
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
    }
    return true;
}

std::string PercentEncode(const std::string& in) {
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(in.size());
    for (unsigned char c : in) {
        const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                                (c >= '0' && c <= '9') ||
                                c == '-' || c == '.' || c == '_' || c == '~';
        if (unreserved) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 0x0F]);
        }
    }
    return out;
}

// Rejects C0 controls and DEL. A label goes straight into list rows and
// dialogs; embedded newlines or escape sequences must never reach the UI.
bool HasControlCharacters(const std::string& text) {
    for (unsigned char c : text) {
        if (c < 0x20 || c == 0x7F) return true;
    }
    return false;
}

bool ParseUnsigned(const std::string& text, uint64_t& out) {
    if (text.empty() || text.size() > 20) return false;
    uint64_t value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
        const uint64_t digit = static_cast<uint64_t>(c - '0');
        if (value > (UINT64_MAX - digit) / 10) return false;   // overflow
        value = value * 10 + digit;
    }
    out = value;
    return true;
}

struct QueryParameter {
    std::string key;
    std::string value;   // still percent-encoded
};

std::vector<QueryParameter> SplitQuery(const std::string& query) {
    std::vector<QueryParameter> items;
    size_t start = 0;
    while (start <= query.size()) {
        size_t end = query.find('&', start);
        if (end == std::string::npos) end = query.size();
        const std::string pair = query.substr(start, end - start);
        if (!pair.empty()) {
            const size_t eq = pair.find('=');
            if (eq == std::string::npos) {
                items.push_back({pair, std::string()});
            } else {
                items.push_back({pair.substr(0, eq), pair.substr(eq + 1)});
            }
        }
        if (end == query.size()) break;
        start = end + 1;
    }
    return items;
}

// Finds a parameter case-insensitively. Duplicates are an error: silently
// taking the first (or last) of two different secrets is exactly the kind of
// ambiguity an attacker would use to make a URI read one way and provision
// another.
bool FindParameter(const std::vector<QueryParameter>& items, const char* key,
                   std::string& outValue, bool& outDuplicate) {
    bool found = false;
    outDuplicate = false;
    for (const auto& item : items) {
        if (EqualsIgnoreCase(item.key, key)) {
            if (found) { outDuplicate = true; return true; }
            outValue = item.value;
            found = true;
        }
    }
    return found;
}

} // namespace

Result ParseOtpAuthUri(const std::string& uri,
                       Parameters& outParams,
                       UltraCryptSecureBuffer& outSecret) {
    outParams = Parameters();
    outSecret.Clear();

    if (uri.empty()) {
        return Result::Error("the provisioning URI is empty");
    }
    if (uri.size() > kMaxUriLength) {
        return Result::Error("the provisioning URI is implausibly long");
    }
    if (HasControlCharacters(uri)) {
        return Result::Error("the provisioning URI contains control characters");
    }

    // Reject the migration format explicitly: it is a protobuf payload, not a
    // Key URI, and must not be mistaken for one.
    if (StartsWithIgnoreCase(uri, "otpauth-migration://")) {
        return Result::Error("this is an account-migration URI, which this "
                             "version does not support");
    }
    if (!StartsWithIgnoreCase(uri, "otpauth://")) {
        return Result::Error("not an otpauth:// provisioning URI");
    }

    const std::string rest = uri.substr(std::strlen("otpauth://"));

    // Split "TYPE/LABEL?QUERY".
    const size_t queryPos = rest.find('?');
    const std::string pathPart  = rest.substr(0, queryPos);
    const std::string queryPart =
        (queryPos == std::string::npos) ? std::string() : rest.substr(queryPos + 1);

    const size_t slashPos = pathPart.find('/');
    if (slashPos == std::string::npos) {
        return Result::Error("the provisioning URI has no label");
    }
    const std::string typeText  = pathPart.substr(0, slashPos);
    const std::string labelText = pathPart.substr(slashPos + 1);

    Type type = Type::Totp;
    if (!ParseTypeName(typeText, type)) {
        return Result::Error("unsupported OTP type in the provisioning URI");
    }
    outParams.type = type;

    // ----- Label: "Issuer:Account" or "Account" -----
    if (labelText.size() > kMaxLabelLength) {
        return Result::Error("the account label is too long");
    }
    std::string label;
    if (!PercentDecode(labelText, label)) {
        return Result::Error("the account label is not correctly encoded");
    }
    if (HasControlCharacters(label)) {
        return Result::Error("the account label contains control characters");
    }

    std::string labelIssuer;
    std::string accountName = label;
    const size_t colon = label.find(':');
    if (colon != std::string::npos) {
        labelIssuer = label.substr(0, colon);
        accountName = label.substr(colon + 1);
        // Some issuers write "Issuer: Account" with a space after the colon.
        while (!accountName.empty() && accountName.front() == ' ') {
            accountName.erase(accountName.begin());
        }
    }
    if (accountName.empty()) {
        return Result::Error("the provisioning URI has no account name");
    }

    // ----- Query parameters -----
    const auto items = SplitQuery(queryPart);
    std::string raw;
    bool duplicate = false;

    // secret (required)
    if (!FindParameter(items, "secret", raw, duplicate)) {
        return Result::Error("the provisioning URI has no secret");
    }
    if (duplicate) {
        return Result::Error("the provisioning URI specifies more than one secret");
    }
    std::string secretText;
    if (!PercentDecode(raw, secretText)) {
        return Result::Error("the secret is not correctly encoded");
    }
    auto decoded = UltraCrypt_Base32Decode(secretText, outSecret);
    UltraCrypt_SecureZero(secretText.data(), secretText.size());
    if (!decoded) {
        outSecret.Clear();
        return Result::Error("the secret is not valid Base32");
    }
    auto secretCheck = ValidateSecret(outSecret);
    if (!secretCheck) {
        outSecret.Clear();
        return secretCheck;
    }

    // issuer (optional). When both the label prefix and the parameter are
    // present the Key URI spec requires them to agree; a mismatch is how a
    // hostile URI would display as one service while being filed under another.
    std::string issuerParam;
    if (FindParameter(items, "issuer", raw, duplicate)) {
        if (duplicate) {
            outSecret.Clear();
            return Result::Error("the provisioning URI specifies more than one issuer");
        }
        if (!PercentDecode(raw, issuerParam)) {
            outSecret.Clear();
            return Result::Error("the issuer is not correctly encoded");
        }
        if (issuerParam.size() > kMaxLabelLength || HasControlCharacters(issuerParam)) {
            outSecret.Clear();
            return Result::Error("the issuer is too long or contains control characters");
        }
    }
    if (!labelIssuer.empty() && !issuerParam.empty() && labelIssuer != issuerParam) {
        outSecret.Clear();
        return Result::Error("the issuer in the label and the issuer parameter "
                             "disagree");
    }
    outParams.issuer      = !issuerParam.empty() ? issuerParam : labelIssuer;
    outParams.accountName = accountName;

    // algorithm (optional, default SHA1)
    if (FindParameter(items, "algorithm", raw, duplicate)) {
        if (duplicate) {
            outSecret.Clear();
            return Result::Error("the provisioning URI specifies more than one algorithm");
        }
        std::string algorithmText;
        if (!PercentDecode(raw, algorithmText) ||
            !ParseAlgorithmName(algorithmText, outParams.algorithm)) {
            outSecret.Clear();
            return Result::Error("unsupported OTP algorithm in the provisioning URI");
        }
    }

    // digits (optional, default 6)
    if (FindParameter(items, "digits", raw, duplicate)) {
        if (duplicate) {
            outSecret.Clear();
            return Result::Error("the provisioning URI specifies more than one digit count");
        }
        uint64_t digits = 0;
        if (!ParseUnsigned(raw, digits) ||
            digits < kMinDigits || digits > kMaxDigits) {
            outSecret.Clear();
            return Result::Error("the digit count must be between 6 and 8");
        }
        outParams.digits = static_cast<uint32_t>(digits);
    }

    // period (TOTP only, default 30)
    if (FindParameter(items, "period", raw, duplicate)) {
        if (duplicate) {
            outSecret.Clear();
            return Result::Error("the provisioning URI specifies more than one period");
        }
        uint64_t period = 0;
        if (!ParseUnsigned(raw, period) ||
            period < kMinPeriodSeconds || period > kMaxPeriodSeconds) {
            outSecret.Clear();
            return Result::Error("the period must be between 15 and 120 seconds");
        }
        outParams.periodSeconds = static_cast<uint32_t>(period);
    }

    // counter (required for HOTP, meaningless for TOTP)
    if (outParams.type == Type::Hotp) {
        if (!FindParameter(items, "counter", raw, duplicate)) {
            outSecret.Clear();
            return Result::Error("a counter-based URI must specify a counter");
        }
        if (duplicate) {
            outSecret.Clear();
            return Result::Error("the provisioning URI specifies more than one counter");
        }
        uint64_t counter = 0;
        if (!ParseUnsigned(raw, counter)) {
            outSecret.Clear();
            return Result::Error("the counter is not a valid number");
        }
        outParams.counter = counter;
    }

    auto paramCheck = ValidateParameters(outParams);
    if (!paramCheck) {
        outSecret.Clear();
        return paramCheck;
    }
    return Result::Ok();
}

Result BuildOtpAuthUri(const Parameters& params,
                       const UltraCryptSecureBuffer& secret,
                       std::string& outUri) {
    outUri.clear();

    auto paramCheck = ValidateParameters(params);
    if (!paramCheck) return paramCheck;
    auto secretCheck = ValidateSecret(secret);
    if (!secretCheck) return secretCheck;
    if (params.accountName.empty()) {
        return Result::Error("an account name is required");
    }
    if (HasControlCharacters(params.accountName) ||
        HasControlCharacters(params.issuer)) {
        return Result::Error("the account name or issuer contains control characters");
    }

    const std::string label =
        params.issuer.empty()
            ? PercentEncode(params.accountName)
            : PercentEncode(params.issuer) + "%3A" + PercentEncode(params.accountName);

    std::string uri = "otpauth://";
    uri += TypeName(params.type);
    uri += "/";
    uri += label;
    uri += "?secret=";
    uri += UltraCanvas::Base32Encode(static_cast<const uint8_t*>(secret.Data()),
                                     secret.GetSize(), /*pad=*/false);

    if (!params.issuer.empty()) {
        uri += "&issuer=" + PercentEncode(params.issuer);
    }
    uri += "&algorithm=";
    uri += AlgorithmName(params.algorithm);
    uri += "&digits=" + std::to_string(params.digits);
    if (params.type == Type::Totp) {
        uri += "&period=" + std::to_string(params.periodSeconds);
    } else {
        uri += "&counter=" + std::to_string(params.counter);
    }

    outUri = std::move(uri);
    return Result::Ok();
}

} // namespace Otp
} // namespace UltraCanvas
