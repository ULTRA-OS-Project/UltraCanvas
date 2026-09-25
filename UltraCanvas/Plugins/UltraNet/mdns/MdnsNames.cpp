// UltraCanvas/Plugins/UltraNet/mdns/MdnsNames.cpp
// DNS-SD name and record arithmetic. See MdnsNames.h.
// Version: 1.0.0
// Author: UltraCanvas Framework / ULTRA OS

#include "MdnsNames.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace UltraCanvas {
namespace Mdns {

namespace {

bool IsDigit(char c) {
    return c >= '0' && c <= '9';
}

// Lower-cases ASCII only. DNS labels compare case-insensitively, and the
// service type may come back from a responder in any case at all.
std::string LowerAscii(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return text;
}

// Walks a presentation-format name and reports where each unescaped dot is.
// Everything else here is built on this, because "where do the labels start"
// is the only question an escape can change the answer to.
std::vector<size_t> UnescapedDots(const std::string& name) {
    std::vector<size_t> dots;
    for (size_t i = 0; i < name.size(); ++i) {
        if (name[i] == '\\') {
            // A backslash escapes the next character whatever it is, so skip
            // it. A trailing backslash escapes nothing and is left alone.
            ++i;
            continue;
        }
        if (name[i] == '.') dots.push_back(i);
    }
    return dots;
}

} // namespace

std::string UnescapeLabel(const std::string& label) {
    std::string out;
    out.reserve(label.size());
    for (size_t i = 0; i < label.size(); ++i) {
        if (label[i] != '\\') {
            out.push_back(label[i]);
            continue;
        }
        if (i + 1 >= label.size()) {
            // Trailing backslash: nothing to escape. Keep it rather than
            // silently dropping a byte the responder sent.
            out.push_back('\\');
            break;
        }
        // \123 - three decimal digits are one byte. Anything shorter or
        // non-numeric falls through to the plain "backslash X means X" case
        // below, which is how an escaped dot and an escaped backslash are
        // handled without needing a branch of their own.
        if (i + 3 < label.size() && IsDigit(label[i + 1]) &&
            IsDigit(label[i + 2]) && IsDigit(label[i + 3])) {
            const int value = (label[i + 1] - '0') * 100 +
                              (label[i + 2] - '0') * 10 +
                              (label[i + 3] - '0');
            if (value <= 255) {
                out.push_back(static_cast<char>(value));
                i += 3;
                continue;
            }
            // Out of range (\256 and up): not an escape, so treat the
            // backslash as escaping just the first digit.
        }
        out.push_back(label[i + 1]);
        ++i;
    }
    return out;
}

std::vector<std::string> SplitName(const std::string& name) {
    std::vector<std::string> labels;
    const std::vector<size_t> dots = UnescapedDots(name);
    size_t start = 0;
    for (const size_t dot : dots) {
        labels.push_back(UnescapeLabel(name.substr(start, dot - start)));
        start = dot + 1;
    }
    if (start <= name.size()) {
        const std::string last = name.substr(start);
        // A trailing dot is a fully-qualified name, not an empty final label.
        if (!last.empty() || labels.empty()) {
            labels.push_back(UnescapeLabel(last));
        }
    }
    return labels;
}

std::string BrowseQueryName(const std::string& serviceType) {
    if (serviceType.empty()) return std::string();
    const std::string lowered = LowerAscii(serviceType);
    if (lowered.size() >= 6 &&
        lowered.compare(lowered.size() - 6, 6, ".local") == 0) {
        return serviceType;
    }
    return serviceType + ".local";
}

std::string ResolveNameFor(const std::string& browsedFullName) {
    // The escaped form, untouched. See the header for why.
    return browsedFullName;
}

bool SplitInstanceName(const std::string& fullName,
                       const std::string& serviceType,
                       std::string& outInstance,
                       std::string& outDomain) {
    outInstance.clear();
    outDomain.clear();
    if (fullName.empty() || serviceType.empty()) return false;

    // The type as labels, with any ".local" the caller tacked on removed: the
    // marker being searched for is the type itself, "_uscan._tcp".
    std::vector<std::string> typeLabels = SplitName(serviceType);
    while (!typeLabels.empty() &&
           LowerAscii(typeLabels.back()) == "local") {
        typeLabels.pop_back();
    }
    if (typeLabels.empty()) return false;

    const std::vector<std::string> labels = SplitName(fullName);
    if (labels.size() <= typeLabels.size()) return false;

    // Find the type's labels in the name. Searched from the right, because an
    // instance is free to be called "_uscan._tcp" itself and the real type is
    // the later one.
    size_t match = labels.size();
    for (size_t start = labels.size() - typeLabels.size() + 1; start-- > 0; ) {
        bool same = true;
        for (size_t i = 0; i < typeLabels.size(); ++i) {
            if (LowerAscii(labels[start + i]) != LowerAscii(typeLabels[i])) {
                same = false;
                break;
            }
        }
        if (same) { match = start; break; }
    }
    if (match == labels.size()) return false;
    if (match == 0) return false;   // a bare type, with no instance in front

    // The instance is every label before the type, rejoined. It is already
    // unescaped, so a dot in it is a real dot; joining with "." is right for
    // something meant to be read, which is all this is used for.
    for (size_t i = 0; i < match; ++i) {
        if (i) outInstance.push_back('.');
        outInstance += labels[i];
    }
    for (size_t i = match + typeLabels.size(); i < labels.size(); ++i) {
        if (!outDomain.empty()) outDomain.push_back('.');
        outDomain += labels[i];
    }
    return true;
}

std::string ServiceTypeOnly(const std::string& serviceType) {
    const std::string lowered = LowerAscii(serviceType);
    if (lowered.size() > 6 &&
        lowered.compare(lowered.size() - 6, 6, ".local") == 0) {
        return serviceType.substr(0, serviceType.size() - 6);
    }
    return serviceType;
}

std::string TrimTrailingDot(const std::string& host) {
    if (host.size() > 1 && host.back() == '.') {
        return host.substr(0, host.size() - 1);
    }
    return host;
}

std::string TxtPair(const std::string& key, const char* value) {
    // No value at all is a different record from an empty value: DNS-SD uses
    // the first as a boolean flag and the second as "this key, set to
    // nothing". Avahi and Bonjour both preserve the difference, so this does.
    if (!value) return key;
    return key + "=" + value;
}

std::string IPv4ToString(uint32_t networkOrderAddress) {
    const uint8_t* b = reinterpret_cast<const uint8_t*>(&networkOrderAddress);
    char buf[16];
    std::snprintf(buf, sizeof buf, "%u.%u.%u.%u",
                  static_cast<unsigned>(b[0]), static_cast<unsigned>(b[1]),
                  static_cast<unsigned>(b[2]), static_cast<unsigned>(b[3]));
    return buf;
}

std::string IPv6ToString(const uint8_t bytes[16]) {
    uint16_t groups[8];
    for (int i = 0; i < 8; ++i) {
        groups[i] = static_cast<uint16_t>((bytes[i * 2] << 8) | bytes[i * 2 + 1]);
    }

    // Longest run of zero groups, leftmost on a tie (RFC 5952 s4.2.3).
    int bestStart = -1, bestLen = 0;
    for (int i = 0; i < 8; ) {
        if (groups[i] != 0) { ++i; continue; }
        int j = i;
        while (j < 8 && groups[j] == 0) ++j;
        if (j - i > bestLen) { bestLen = j - i; bestStart = i; }
        i = j;
    }
    // A single zero group is written out, not compressed (RFC 5952 s4.2.2).
    if (bestLen < 2) { bestStart = -1; bestLen = 0; }

    std::string out;
    for (int i = 0; i < 8; ) {
        if (i == bestStart) {
            out += "::";
            i += bestLen;
            continue;
        }
        if (!out.empty() && out.back() != ':') out.push_back(':');
        char buf[8];
        std::snprintf(buf, sizeof buf, "%x", groups[i]);
        out += buf;
        ++i;
    }
    if (out.empty()) out = "::";
    return out;
}

} // namespace Mdns
} // namespace UltraCanvas
