// core/UltraNet/UltraNetFlatJson.h
// A scanner for flat JSON objects - the token-endpoint bodies of OAuth2 and
// the answers of the OAuth2 sign-in service, which are one level of scalars
// by design. It walks the top-level object, decodes string values (\uXXXX
// included), skips nested objects and arrays whole, and reports each
// top-level key with a scalar value to the visitor. Internal to UltraNet,
// which must not pull a JSON engine into its target; not part of the public
// surface.
// Version: 1.0.0 - moved out of UltraNetOAuth2.cpp, shared with UltraNetOAuth2Service.cpp
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace ultranet_internal {
namespace flatjson_detail {

inline void SkipWs(const std::string& s, std::size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
}

inline void AppendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += char(cp);
    } else if (cp < 0x800) {
        out += char(0xC0 | (cp >> 6));
        out += char(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += char(0xE0 | (cp >> 12));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    } else {
        out += char(0xF0 | (cp >> 18));
        out += char(0x80 | ((cp >> 12) & 0x3F));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    }
}

inline bool ParseJsonString(const std::string& s, std::size_t& i, std::string& out) {
    if (i >= s.size() || s[i] != '"') return false;
    ++i;
    out.clear();
    while (i < s.size()) {
        char c = s[i];
        if (c == '"') { ++i; return true; }
        if (c == '\\') {
            if (++i >= s.size()) return false;
            char e = s[i++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    auto hex4 = [&](uint32_t& v) {
                        if (i + 4 > s.size()) return false;
                        v = 0;
                        for (int k = 0; k < 4; ++k) {
                            char hc = s[i + k];
                            v <<= 4;
                            if (hc >= '0' && hc <= '9') v |= uint32_t(hc - '0');
                            else if (hc >= 'a' && hc <= 'f') v |= uint32_t(hc - 'a' + 10);
                            else if (hc >= 'A' && hc <= 'F') v |= uint32_t(hc - 'A' + 10);
                            else return false;
                        }
                        i += 4;
                        return true;
                    };
                    uint32_t cp = 0;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < s.size() &&
                        s[i] == '\\' && s[i + 1] == 'u') {
                        i += 2;
                        uint32_t lo = 0;
                        if (!hex4(lo)) return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    AppendUtf8(out, cp);
                    break;
                }
                default: return false;
            }
        } else {
            out += c;
            ++i;
        }
    }
    return false;
}

inline bool SkipJsonComposite(const std::string& s, std::size_t& i, char open, char close) {
    ++i; // consume opener
    int depth = 1;
    while (i < s.size() && depth > 0) {
        SkipWs(s, i);
        if (i >= s.size()) return false;
        char c = s[i];
        if (c == '"') {
            std::string ignore;
            if (!ParseJsonString(s, i, ignore)) return false;
        } else {
            if (c == open) ++depth;
            else if (c == close) --depth;
            ++i;
        }
    }
    return depth == 0;
}

// Skips any JSON value; captures scalars (string decoded, number/bool as
// spelled) into outScalar and reports whether the value was a scalar.
inline bool SkipJsonValue(const std::string& s, std::size_t& i,
                          std::string& outScalar, bool& outIsScalar) {
    SkipWs(s, i);
    if (i >= s.size()) return false;
    outIsScalar = false;
    char c = s[i];
    if (c == '"') {
        outIsScalar = true;
        return ParseJsonString(s, i, outScalar);
    }
    if (c == '{') return SkipJsonComposite(s, i, '{', '}');
    if (c == '[') return SkipJsonComposite(s, i, '[', ']');
    std::size_t start = i;
    while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']' &&
           s[i] != ' ' && s[i] != '\t' && s[i] != '\n' && s[i] != '\r') {
        ++i;
    }
    if (i == start) return false;
    outScalar = s.substr(start, i - start);
    outIsScalar = true;
    return true;
}

} // namespace flatjson_detail

// Visits every top-level key whose value is a scalar (a string, decoded; a
// number or a bool as spelled; null is skipped). Returns false when `json`
// is not a well-formed object.
inline bool ForEachTopLevelField(
    const std::string& json,
    const std::function<void(const std::string& key, const std::string& value)>& visit) {
    using namespace flatjson_detail;
    std::size_t i = 0;
    SkipWs(json, i);
    if (i >= json.size() || json[i] != '{') return false;
    ++i;
    SkipWs(json, i);
    if (i < json.size() && json[i] == '}') return true;
    while (i < json.size()) {
        SkipWs(json, i);
        std::string key;
        if (!ParseJsonString(json, i, key)) return false;
        SkipWs(json, i);
        if (i >= json.size() || json[i] != ':') return false;
        ++i;
        std::string value;
        bool isScalar = false;
        if (!SkipJsonValue(json, i, value, isScalar)) return false;
        if (isScalar && value != "null") visit(key, value);
        SkipWs(json, i);
        if (i >= json.size()) return false;
        if (json[i] == ',') { ++i; continue; }
        if (json[i] == '}') return true;
        return false;
    }
    return false;
}

} // namespace ultranet_internal
