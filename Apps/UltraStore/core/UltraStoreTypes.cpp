// Apps/UltraStore/core/UltraStoreTypes.cpp
// Targets, versions, localized text and hex (UltraStoreTypes.h).
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraStoreTypes.h"

#include <algorithm>
#include <cctype>

namespace UltraStore {

namespace {

std::string Lower(std::string_view text) {
    std::string out(text);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool AllDigits(std::string_view part) {
    if (part.empty()) return false;
    for (char c : part) if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    return true;
}

// "1.2.3-beta.1" -> {"1","2","3"}, "beta.1"
void SplitVersion(std::string_view version, std::vector<std::string>& parts, std::string& pre) {
    parts.clear();
    pre.clear();
    const size_t dash = version.find('-');
    if (dash != std::string_view::npos) {
        pre = std::string(version.substr(dash + 1));
        version = version.substr(0, dash);
    }
    size_t start = 0;
    while (start <= version.size()) {
        const size_t dot = version.find('.', start);
        const size_t end = dot == std::string_view::npos ? version.size() : dot;
        parts.emplace_back(version.substr(start, end - start));
        if (dot == std::string_view::npos) break;
        start = dot + 1;
    }
}

int ComparePart(const std::string& a, const std::string& b) {
    const bool na = AllDigits(a), nb = AllDigits(b);
    if (na && nb) {
        // Leading zeros do not matter: compare as numbers of any length.
        const std::string ta = a.substr(std::min(a.find_first_not_of('0'), a.size() - 1));
        const std::string tb = b.substr(std::min(b.find_first_not_of('0'), b.size() - 1));
        if (ta.size() != tb.size()) return ta.size() < tb.size() ? -1 : 1;
        return ta.compare(tb) < 0 ? -1 : (ta == tb ? 0 : 1);
    }
    if (na != nb) return na ? -1 : 1;   // numbers sort before words
    return a.compare(b) < 0 ? -1 : (a == b ? 0 : 1);
}

} // namespace

OS ParseOS(std::string_view text) {
    const std::string t = Lower(text);
    if (t == "linux")   return OS::Linux;
    if (t == "windows") return OS::Windows;
    if (t == "macos")   return OS::MacOS;
    if (t == "ultraos") return OS::UltraOS;
    if (t == "ios")     return OS::IOS;
    if (t == "android") return OS::Android;
    if (t == "wasm" || t == "web" || t == "browser") return OS::Wasm;
    return OS::Unknown;
}

std::string OSName(OS os) {
    switch (os) {
        case OS::Linux:   return "linux";
        case OS::Windows: return "windows";
        case OS::MacOS:   return "macos";
        case OS::UltraOS: return "ultraos";
        case OS::IOS:     return "ios";
        case OS::Android: return "android";
        case OS::Wasm:    return "wasm";
        case OS::Unknown: break;
    }
    return "unknown";
}

Arch ParseArch(std::string_view text) {
    const std::string t = Lower(text);
    if (t == "x86_64" || t == "amd64" || t == "x64") return Arch::X86_64;
    if (t == "arm64" || t == "aarch64")              return Arch::Arm64;
    if (t == "armv7" || t == "armv7l" || t == "armhf") return Arch::Armv7;
    if (t == "riscv64")                               return Arch::Riscv64;
    if (t == "wasm32" || t == "wasm")                 return Arch::Wasm32;
    if (t == "universal")                             return Arch::Universal;
    if (t == "any")                                   return Arch::Any;
    return Arch::Unknown;
}

std::string ArchName(Arch arch) {
    switch (arch) {
        case Arch::X86_64:    return "x86_64";
        case Arch::Arm64:     return "arm64";
        case Arch::Armv7:     return "armv7";
        case Arch::Riscv64:   return "riscv64";
        case Arch::Wasm32:    return "wasm32";
        case Arch::Universal: return "universal";
        case Arch::Any:       return "any";
        case Arch::Unknown:   break;
    }
    return "unknown";
}

std::string Target::Describe() const {
    std::string out = OSName(os) + "/" + ArchName(arch);
    if (!variants.empty()) {
        out += " (";
        for (size_t i = 0; i < variants.size(); ++i) {
            if (i) out += ", ";
            out += variants[i];
        }
        out += ")";
    }
    return out;
}

int CompareVersions(std::string_view a, std::string_view b) {
    std::vector<std::string> pa, pb;
    std::string prea, preb;
    SplitVersion(a, pa, prea);
    SplitVersion(b, pb, preb);
    const size_t n = std::max(pa.size(), pb.size());
    for (size_t i = 0; i < n; ++i) {
        const std::string& x = i < pa.size() ? pa[i] : "0";
        const std::string& y = i < pb.size() ? pb[i] : "0";
        const int c = ComparePart(x, y);
        if (c != 0) return c;
    }
    // Same numbers: a release is newer than its pre-release; two pre-releases
    // compare as text.
    if (prea.empty() != preb.empty()) return prea.empty() ? 1 : -1;
    return prea.compare(preb) < 0 ? -1 : (prea == preb ? 0 : 1);
}

bool VersionLess(std::string_view a, std::string_view b)  { return CompareVersions(a, b) < 0; }
bool VersionEqual(std::string_view a, std::string_view b) { return CompareVersions(a, b) == 0; }

std::string LocalizedText::Text(const std::string& language) const {
    auto it = byLanguage.find(language);
    if (it != byLanguage.end()) return it->second;
    // "de-AT" -> "de"
    const size_t dash = language.find('-');
    if (dash != std::string::npos) {
        it = byLanguage.find(language.substr(0, dash));
        if (it != byLanguage.end()) return it->second;
    }
    it = byLanguage.find("en");
    if (it != byLanguage.end()) return it->second;
    return byLanguage.empty() ? std::string() : byLanguage.begin()->second;
}

std::string ErrorCodeName(ErrorCode code) {
    switch (code) {
        case ErrorCode::Ok:              return "Ok";
        case ErrorCode::InvalidArgument:   return "InvalidArgument";
        case ErrorCode::ParseError:        return "ParseError";
        case ErrorCode::SchemaTooNew:      return "SchemaTooNew";
        case ErrorCode::SignatureInvalid:  return "SignatureInvalid";
        case ErrorCode::SequenceRollback:  return "SequenceRollback";
        case ErrorCode::NotFound:          return "NotFound";
        case ErrorCode::NotAvailable:      return "NotAvailable";
        case ErrorCode::DependencyError:   return "DependencyError";
        case ErrorCode::NetworkError:      return "NetworkError";
        case ErrorCode::IoError:           return "IoError";
        case ErrorCode::CryptoUnavailable: return "CryptoUnavailable";
        case ErrorCode::Internal:          break;
    }
    return "Internal";
}

std::string ToHex(const std::vector<uint8_t>& bytes) {
    static const char* kDigits = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (uint8_t b : bytes) {
        out.push_back(kDigits[b >> 4]);
        out.push_back(kDigits[b & 0x0F]);
    }
    return out;
}

bool FromHex(std::string_view hex, std::vector<uint8_t>& out) {
    if (hex.size() % 2 != 0) return false;
    std::vector<uint8_t> bytes;
    bytes.reserve(hex.size() / 2);
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < hex.size(); i += 2) {
        const int hi = nibble(hex[i]), lo = nibble(hex[i + 1]);
        if (hi < 0 || lo < 0) return false;
        bytes.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    out = std::move(bytes);
    return true;
}

} // namespace UltraStore
