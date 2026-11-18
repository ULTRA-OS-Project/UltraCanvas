// Plugins/UltraIM/xmpp/XmppXml.cpp
// XMPP - stanzas as small XML trees: tinyxml2 reads them, Serialize writes them escaped
// Version: 0.1.0
// Last Modified: 2026-11-17
// Author: UltraCanvas Framework

#include "XmppXml.h"

#include "UltraCanvasTextUtils.h"
#include "UltraCrypt/UltraCryptCore.h"

#include <tinyxml2.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <map>

namespace UltraIMXmpp {

// ============================================================================
// ELEMENTS
// ============================================================================

std::string XmlElement::Attr(const std::string& key) const {
    for (const auto& [k, v] : attrs) {
        if (k == key) return v;
    }
    return std::string();
}

bool XmlElement::HasAttr(const std::string& key) const {
    for (const auto& kv : attrs) {
        if (kv.first == key) return true;
    }
    return false;
}

XmlElement& XmlElement::Set(const std::string& key, const std::string& value) {
    for (auto& kv : attrs) {
        if (kv.first == key) {
            kv.second = value;
            return *this;
        }
    }
    attrs.emplace_back(key, value);
    return *this;
}

const XmlElement* XmlElement::Child(const std::string& localName, const std::string& nameSpace) const {
    for (const XmlElement& c : children) {
        if (c.Is(localName, nameSpace)) return &c;
    }
    return nullptr;
}

XmlElement* XmlElement::Child(const std::string& localName, const std::string& nameSpace) {
    for (XmlElement& c : children) {
        if (c.Is(localName, nameSpace)) return &c;
    }
    return nullptr;
}

std::vector<const XmlElement*> XmlElement::Children(const std::string& localName, const std::string& nameSpace) const {
    std::vector<const XmlElement*> out;
    for (const XmlElement& c : children) {
        if (c.Is(localName, nameSpace)) out.push_back(&c);
    }
    return out;
}

std::string XmlElement::ChildText(const std::string& localName, const std::string& nameSpace) const {
    const XmlElement* c = Child(localName, nameSpace);
    return c ? c->text : std::string();
}

XmlElement& XmlElement::Add(const std::string& localName, const std::string& nameSpace) {
    children.emplace_back(localName, nameSpace.empty() ? ns : nameSpace);
    return children.back();
}

XmlElement& XmlElement::Add(XmlElement child) {
    if (child.ns.empty()) child.ns = ns;
    children.push_back(std::move(child));
    return children.back();
}

std::string XmlElement::Serialize(const std::string& parentNs) const {
    std::string out = "<" + name;
    if (!ns.empty() && ns != parentNs) out += " xmlns=\"" + XmlEscape(ns) + "\"";
    for (const auto& [k, v] : attrs) out += " " + k + "=\"" + XmlEscape(v) + "\"";
    if (text.empty() && children.empty()) return out + "/>";
    out += ">";
    out += XmlEscape(text);
    for (const XmlElement& c : children) out += c.Serialize(ns);
    return out + "</" + name + ">";
}

std::string XmlEscape(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (unsigned char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default:
                // XML 1.0 has no place for the other control characters.
                if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') break;
                out += static_cast<char>(c);
        }
    }
    return out;
}

namespace {

using Scope = std::map<std::string, std::string>;     // prefix ("" = default) -> namespace

void Convert(const tinyxml2::XMLElement* in, const Scope& outer, XmlElement& out) {
    Scope scope = outer;
    for (const tinyxml2::XMLAttribute* a = in->FirstAttribute(); a; a = a->Next()) {
        const std::string key = a->Name();
        if (key == "xmlns") scope[""] = a->Value();
        else if (key.rfind("xmlns:", 0) == 0) scope[key.substr(6)] = a->Value();
    }
    const std::string qname = in->Name();
    const size_t colon = qname.find(':');
    const std::string prefix = colon == std::string::npos ? std::string() : qname.substr(0, colon);
    out.name = colon == std::string::npos ? qname : qname.substr(colon + 1);
    auto found = scope.find(prefix);
    out.ns = found != scope.end() ? found->second : std::string();
    for (const tinyxml2::XMLAttribute* a = in->FirstAttribute(); a; a = a->Next()) {
        const std::string key = a->Name();
        if (key == "xmlns" || key.rfind("xmlns:", 0) == 0) continue;
        out.attrs.emplace_back(key, a->Value());
    }
    for (const tinyxml2::XMLNode* n = in->FirstChild(); n; n = n->NextSibling()) {
        if (const tinyxml2::XMLElement* e = n->ToElement()) {
            out.children.emplace_back();
            Convert(e, scope, out.children.back());
        } else if (const tinyxml2::XMLText* t = n->ToText()) {
            if (t->Value()) out.text += t->Value();
        }
    }
}

} // namespace

bool XmlParse(const std::string& text, XmlElement& out, std::string* error) {
    tinyxml2::XMLDocument doc(true, tinyxml2::PRESERVE_WHITESPACE);
    if (doc.Parse(text.data(), text.size()) != tinyxml2::XML_SUCCESS) {
        if (error) *error = doc.ErrorStr() ? doc.ErrorStr() : "not XML";
        return false;
    }
    const tinyxml2::XMLElement* root = doc.RootElement();
    if (!root) {
        if (error) *error = "no element";
        return false;
    }
    out = XmlElement();
    Scope scope;
    scope["xml"] = "http://www.w3.org/XML/1998/namespace";
    Convert(root, scope, out);
    return true;
}

// ============================================================================
// JIDS
// ============================================================================

namespace {

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

} // namespace

std::string JidBare(const std::string& jid) {
    const size_t slash = jid.find('/');
    return Lower(slash == std::string::npos ? jid : jid.substr(0, slash));
}

std::string JidResource(const std::string& jid) {
    const size_t slash = jid.find('/');
    return slash == std::string::npos ? std::string() : jid.substr(slash + 1);
}

std::string JidLocal(const std::string& jid) {
    const std::string bare = JidBare(jid);
    const size_t at = bare.find('@');
    return at == std::string::npos ? std::string() : bare.substr(0, at);
}

std::string JidDomain(const std::string& jid) {
    const std::string bare = JidBare(jid);
    const size_t at = bare.find('@');
    return at == std::string::npos ? bare : bare.substr(at + 1);
}

std::string JidNormalize(const std::string& address) {
    std::string a = Trim(address);
    if (a.rfind("xmpp:", 0) == 0) a = a.substr(5);
    const size_t query = a.find('?');
    if (query != std::string::npos) a = a.substr(0, query);
    if (a.empty() || a.find_first_of(" \t\r\n<>\"'&") != std::string::npos) return std::string();
    const size_t slash = a.find('/');
    const std::string resource = slash == std::string::npos ? std::string() : a.substr(slash);
    std::string bare = Lower(slash == std::string::npos ? a : a.substr(0, slash));
    const size_t at = bare.find('@');
    if (at == 0 || at == bare.size() - 1 || bare.find('@', at == std::string::npos ? 0 : at + 1) != std::string::npos)
        return std::string();
    if (JidDomain(bare).find('.') == std::string::npos && JidDomain(bare) != "localhost") return std::string();
    return bare + resource;
}

// ============================================================================
// SMALL THINGS
// ============================================================================

std::string Base64(const std::vector<uint8_t>& bytes) {
    return UltraCanvas::Base64Encode(bytes, false);
}

std::string Base64(const std::string& bytes) {
    return Base64(std::vector<uint8_t>(bytes.begin(), bytes.end()));
}

std::vector<uint8_t> Unbase64(const std::string& text) {
    return UltraCanvas::Base64Decode(text);
}

std::string RandomId(size_t length) {
    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::vector<uint8_t> bytes;
    UltraCrypt_RandomBytes(bytes, length);
    std::string out;
    for (size_t i = 0; i < length; ++i) {
        const uint8_t b = i < bytes.size() ? bytes[i] : static_cast<uint8_t>(std::rand());
        out += alphabet[b % 36];
    }
    return out;
}

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}

namespace {

// Days since 1970-01-01 of a civil date (Howard Hinnant's algorithm).
int64_t DaysFromCivil(int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

} // namespace

int64_t ParseXmppTime(const std::string& stamp) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    if (std::sscanf(stamp.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s) != 6) return 0;
    int64_t ms = (DaysFromCivil(y, static_cast<unsigned>(mo), static_cast<unsigned>(d)) * 86400 + h * 3600 + mi * 60 + s) * 1000;
    size_t i = stamp.find('T');
    i = i == std::string::npos ? stamp.size() : i + 9;
    if (i < stamp.size() && stamp[i] == '.') {
        int64_t frac = 0, scale = 1000;
        for (++i; i < stamp.size() && std::isdigit(static_cast<unsigned char>(stamp[i])); ++i) {
            if (scale > 1) {
                scale /= 10;
                frac += (stamp[i] - '0') * scale;
            }
        }
        ms += frac;
    }
    if (i < stamp.size() && (stamp[i] == '+' || stamp[i] == '-')) {
        int oh = 0, om = 0;
        std::sscanf(stamp.c_str() + i + 1, "%d:%d", &oh, &om);
        const int64_t offset = (oh * 3600 + om * 60) * 1000;
        ms += stamp[i] == '+' ? -offset : offset;
    }
    return ms;
}

std::string FormatXmppTime(int64_t ms) {
    const std::time_t t = static_cast<std::time_t>(ms / 1000);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", tm.tm_year + 1900, tm.tm_mon + 1,
                  tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms % 1000));
    return buffer;
}

std::string Utf8Erase(const std::string& text, size_t fromCodePoint, size_t toCodePoint) {
    std::string out;
    size_t cp = 0;
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        const size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
        if (cp < fromCodePoint || cp >= toCodePoint) out.append(text, i, len);
        i += len;
        ++cp;
    }
    return out;
}

} // namespace UltraIMXmpp
