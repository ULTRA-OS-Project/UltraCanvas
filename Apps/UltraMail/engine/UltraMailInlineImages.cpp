// Apps/UltraMail/engine/UltraMailInlineImages.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraMailInlineImages.h"

#include <UltraNet/UltraNetMime.h>

#include <cctype>

namespace UltraMail {

namespace {

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string Trim(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

int Hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string PercentDecode(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() && Hex(s[i + 1]) >= 0 && Hex(s[i + 2]) >= 0) {
            out += static_cast<char>(Hex(s[i + 1]) * 16 + Hex(s[i + 2]));
            i += 2;
        } else {
            out += s[i];
        }
    }
    return out;
}

std::string StripAngles(std::string s) {
    s = Trim(s);
    if (s.size() >= 2 && s.front() == '<' && s.back() == '>') s = s.substr(1, s.size() - 2);
    return s;
}

std::string HeaderOf(const UltraNetMimePart& part, const std::string& name) {
    const std::string want = Lower(name);
    for (const auto& h : part.headers) if (Lower(h.first) == want) return h.second;
    return "";
}

void Walk(const UltraNetMimePart& part, InlineImages& out) {
    if (!part.isMultipart && !part.body.empty()) {
        if (!part.contentId.empty())
            out.byContentId[StripAngles(part.contentId)] = part.body;
        const std::string location = Trim(HeaderOf(part, "Content-Location"));
        if (!location.empty()) out.byContentLocation[location] = part.body;
    }
    for (const auto& child : part.children) Walk(child, out);
}

bool StartsWithNoCase(const std::string& s, const char* prefix) {
    const std::string p(prefix);
    return s.size() >= p.size() && Lower(s.substr(0, p.size())) == p;
}

} // namespace

InlineImages CollectInlineImages(const std::string& rawMessage) {
    InlineImages out;
    UltraNetMimeMessage msg;
    if (rawMessage.empty() || !UltraNet_MimeParse(rawMessage, msg)) return out;
    Walk(msg.root, out);
    return out;
}

ImageSource ClassifyImageSource(const std::string& srcIn, const InlineImages& images) {
    const std::string src = Trim(srcIn);
    if (StartsWithNoCase(src, "cid:") || StartsWithNoCase(src, "data:")) return ImageSource::Embedded;
    if (images.byContentLocation.count(src)) return ImageSource::Embedded;
    if (StartsWithNoCase(src, "http://") || StartsWithNoCase(src, "https://"))
        return ImageSource::Remote;
    // Protocol-relative ("//host/x.png"): remote, over https.
    if (src.size() > 2 && src[0] == '/' && src[1] == '/') return ImageSource::Remote;
    return ImageSource::Other;
}

std::vector<uint8_t> ResolveEmbeddedImage(const std::string& srcIn, const InlineImages& images) {
    const std::string src = Trim(srcIn);
    if (StartsWithNoCase(src, "cid:")) {
        const std::string id = StripAngles(PercentDecode(src.substr(4)));
        if (auto it = images.byContentId.find(id); it != images.byContentId.end()) return it->second;
        // Content-IDs are compared exactly by the RFC; some mailers change
        // the case between the reference and the part.
        const std::string lower = Lower(id);
        for (const auto& [cid, bytes] : images.byContentId)
            if (Lower(cid) == lower) return bytes;
        return {};
    }
    if (StartsWithNoCase(src, "data:")) {
        const std::size_t comma = src.find(',');
        if (comma == std::string::npos) return {};
        const std::string meta = Lower(src.substr(5, comma - 5));
        const std::string payload = src.substr(comma + 1);
        if (meta.find(";base64") != std::string::npos) {
            std::string compact;
            for (char c : payload) if (!std::isspace(static_cast<unsigned char>(c))) compact += c;
            std::vector<uint8_t> bytes;
            if (!UltraNet_Base64Decode(compact, bytes)) return {};
            return bytes;
        }
        const std::string text = PercentDecode(payload);
        return std::vector<uint8_t>(text.begin(), text.end());
    }
    if (auto it = images.byContentLocation.find(src); it != images.byContentLocation.end())
        return it->second;
    return {};
}

} // namespace UltraMail
