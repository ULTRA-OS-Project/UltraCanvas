// UltraSite/core/UltraSiteAssets.cpp
// What every asset goes through: its content hash, its compressed size, and
// for the stylesheet and the scripts the light minifying that keeps them
// small without a toolchain. The tokens' CSS is written here too.
// Version: 0.1.0
// Last Modified: 2026-11-17
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraSite/UltraSite.h"
#include "UltraSiteInternal.h"

#include "UltraCanvasTextUtils.h"
#include "UltraCrypt/UltraCryptCore.h"

#include <cctype>
#include <cstdint>
#include <cstdio>

#if ULTRASITE_HAS_ZLIB
#include <zlib.h>
#endif

namespace UltraSite {

namespace {

std::string Fnv1aHex(const std::string& content) {
    uint64_t hash = 1469598103934665603ULL;
    for (unsigned char c : content) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    char buffer[17];
    std::snprintf(buffer, sizeof buffer, "%016llx", static_cast<unsigned long long>(hash));   // locale-ok: hex digits only
    return buffer;
}

bool IsTokenName(const std::string& name) {
    if (name.empty()) return false;
    for (char c : name) {
        if (!(std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) || c == '-')) return false;
    }
    return true;
}

bool IsTokenValue(const std::string& value) {
    if (value.empty()) return false;
    for (char c : value) {
        if (c == ';' || c == '{' || c == '}' || c == '<' || c == '>' || c == '\n' || c == '\r') return false;
    }
    return true;
}

std::string Declarations(const std::vector<std::pair<std::string, std::string>>& values, std::vector<std::string>& errors) {
    std::string out;
    for (const auto& [name, value] : values) {
        if (!IsTokenName(name)) {
            errors.push_back("token name '" + name + "' is not [a-z0-9-]");
            continue;
        }
        if (!IsTokenValue(value)) {
            errors.push_back("token '" + name + "' has a value with ; { } < > or a line break");
            continue;
        }
        out += "--us-" + name + ":" + value + ";";
    }
    return out;
}

} // namespace

bool Sha256(const std::string& content, std::vector<uint8_t>& digest) {
    return static_cast<bool>(UltraCrypt_Hash(UltraCryptHashAlgorithm::SHA256, content.data(), content.size(), digest)) &&
           digest.size() == 32;
}

std::string ContentHash(const std::string& content, size_t length) {
    std::vector<uint8_t> digest;
    std::string hex = Sha256(content, digest) ? UltraCrypt_ToHex(digest) : Fnv1aHex(content);
    if (length < hex.size()) hex.resize(length);
    return hex;
}

bool HashAvailable() {
    std::vector<uint8_t> digest;
    return Sha256("", digest);
}

std::string CspHash(const std::string& inlineScript) {
    std::vector<uint8_t> digest;
    if (!Sha256(inlineScript, digest)) return std::string();
    return "'sha256-" + UltraCanvas::Base64Encode(digest, false) + "'";
}

size_t CompressedSize(const std::string& content) {
#if ULTRASITE_HAS_ZLIB
    uLongf size = compressBound(static_cast<uLong>(content.size()));
    std::string buffer(size, '\0');
    if (compress2(reinterpret_cast<Bytef*>(buffer.data()), &size, reinterpret_cast<const Bytef*>(content.data()),
                  static_cast<uLong>(content.size()), 9) == Z_OK) {
        // zlib's 2-byte header and 4-byte trailer against gzip's 10 and 8.
        return static_cast<size_t>(size) + 12;
    }
#endif
    return content.size();
}

bool CompressionAvailable() {
#if ULTRASITE_HAS_ZLIB
    return true;
#else
    return false;
#endif
}

std::string TokensCss(const Tokens& tokens, std::vector<std::string>& errors) {
    std::string out;
    const std::string light = Declarations(tokens.light, errors);
    if (!light.empty()) out += ":root{" + light + "}";
    if (!tokens.dark.empty()) {
        const std::string dark = Declarations(tokens.dark, errors) + "color-scheme:dark;";
        // No choice stored: the visitor's system decides. A choice stored by
        // the toggle (data-theme, set before the first paint) wins.
        out += "@media (prefers-color-scheme:dark){:root:not([data-theme=light]){" + dark + "}}";
        out += ":root[data-theme=dark]{" + dark + "}";
    }
    return out;
}

std::string MinifyCss(const std::string& css) {
    std::string out;
    out.reserve(css.size());
    char quote = 0;
    bool pendingSpace = false;
    auto isPunct = [](char c) { return c == '{' || c == '}' || c == ';' || c == ',' || c == '>'; };
    for (size_t i = 0; i < css.size(); ++i) {
        const char c = css[i];
        if (quote) {
            out += c;
            if (c == '\\' && i + 1 < css.size()) out += css[++i];
            else if (c == quote) quote = 0;
            continue;
        }
        if (c == '/' && i + 1 < css.size() && css[i + 1] == '*') {
            const size_t end = css.find("*/", i + 2);
            i = end == std::string::npos ? css.size() : end + 1;
            pendingSpace = true;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c))) {
            pendingSpace = true;
            continue;
        }
        if (pendingSpace && !out.empty() && !isPunct(out.back()) && !isPunct(c)) out += ' ';
        pendingSpace = false;
        if (c == '}' && !out.empty() && out.back() == ';') out.pop_back();
        if (c == '"' || c == '\'') quote = c;
        out += c;
    }
    return out;
}

std::string StripScript(const std::string& js) {
    std::string out;
    out.reserve(js.size());
    size_t start = 0;
    while (start <= js.size()) {
        size_t end = js.find('\n', start);
        if (end == std::string::npos) end = js.size();
        std::string line = js.substr(start, end - start);
        size_t first = 0;
        while (first < line.size() && (line[first] == ' ' || line[first] == '\t')) ++first;
        size_t last = line.size();
        while (last > first && (line[last - 1] == ' ' || line[last - 1] == '\t' || line[last - 1] == '\r')) --last;
        line = line.substr(first, last - first);
        if (!line.empty() && line.rfind("//", 0) != 0) {
            out += line;
            out += '\n';
        }
        start = end + 1;
    }
    return out;
}

} // namespace UltraSite
