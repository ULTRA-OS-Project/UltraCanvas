// core/UltraNet/UltraNetMime.cpp
// Implementation of the UltraNet MIME utility: transfer-encoding codecs,
// RFC 2047 header words, message parsing into a decoded part tree, and message
// building. Pure C++ / STL — no libcurl or platform dependency.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraNet/UltraNetMime.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace {

// ---- small helpers ---------------------------------------------------------

std::string Lower(const std::string& s) {
    std::string r; r.reserve(s.size());
    for (char c : s) r.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return r;
}

std::string Trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b-1]))) --b;
    return s.substr(a, b - a);
}

int HexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// ISO-8859-1 / Windows-1252 byte stream -> UTF-8.
std::string Latin1ToUtf8(const std::string& in) {
    std::string out; out.reserve(in.size());
    for (unsigned char b : in) {
        if (b < 0x80) out.push_back(static_cast<char>(b));
        else { out.push_back(static_cast<char>(0xC0 | (b >> 6)));
               out.push_back(static_cast<char>(0x80 | (b & 0x3F))); }
    }
    return out;
}

std::string CharsetToUtf8(const std::string& bytes, const std::string& charsetIn) {
    std::string cs = Lower(Trim(charsetIn));
    auto star = cs.find('*');                 // strip RFC 2231 language tag
    if (star != std::string::npos) cs = cs.substr(0, star);
    if (cs.empty() || cs == "utf-8" || cs == "utf8" || cs == "us-ascii" || cs == "ascii")
        return bytes;
    if (cs == "iso-8859-1" || cs == "iso8859-1" || cs == "latin1" ||
        cs == "windows-1252" || cs == "cp1252")
        return Latin1ToUtf8(bytes);
    return bytes;   // best effort for other charsets
}

// ---- base64 ----------------------------------------------------------------

const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string Base64EncodeImpl(const std::vector<uint8_t>& in, bool wrap) {
    std::string out;
    std::size_t lineWidth = 0;
    for (std::size_t i = 0; i < in.size(); i += 3) {
        const std::size_t left = in.size() - i;
        const uint32_t t = (static_cast<uint32_t>(in[i]) << 16) |
                           (left > 1 ? static_cast<uint32_t>(in[i+1]) << 8 : 0) |
                           (left > 2 ? static_cast<uint32_t>(in[i+2])      : 0);
        out.push_back(kB64[(t >> 18) & 0x3f]);
        out.push_back(kB64[(t >> 12) & 0x3f]);
        out.push_back(left > 1 ? kB64[(t >> 6) & 0x3f] : '=');
        out.push_back(left > 2 ? kB64[ t       & 0x3f] : '=');
        lineWidth += 4;
        if (wrap && lineWidth >= 76) { out.append("\r\n"); lineWidth = 0; }
    }
    if (wrap && lineWidth > 0) out.append("\r\n");
    return out;
}

int B64Val(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

bool Base64DecodeImpl(const std::string& text, std::vector<uint8_t>& out) {
    out.clear();
    int buf = 0, bits = 0;
    for (unsigned char c : text) {
        if (c == '=' || std::isspace(c)) continue;
        int v = B64Val(c);
        if (v < 0) continue;               // skip stray chars
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back(static_cast<uint8_t>((buf >> bits) & 0xFF)); }
    }
    return true;
}

// ---- quoted-printable ------------------------------------------------------

std::string QpDecodeImpl(const std::string& in) {
    std::string out;
    for (std::size_t i = 0; i < in.size(); ++i) {
        char c = in[i];
        if (c == '=') {
            if (i + 1 < in.size() && (in[i+1] == '\r' || in[i+1] == '\n')) {
                // soft line break
                if (in[i+1] == '\r' && i + 2 < in.size() && in[i+2] == '\n') i += 2;
                else i += 1;
                continue;
            }
            if (i + 2 < in.size()) {
                int hi = HexVal(in[i+1]), lo = HexVal(in[i+2]);
                if (hi >= 0 && lo >= 0) { out.push_back(static_cast<char>((hi << 4) | lo)); i += 2; continue; }
            }
            out.push_back(c);   // stray '='
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::string QpEncodeImpl(const std::string& in) {
    static const char* HEX = "0123456789ABCDEF";
    std::string out;
    std::size_t col = 0;
    auto emit = [&](const std::string& s) {
        if (col + s.size() > 75) { out.append("=\r\n"); col = 0; }
        out += s; col += s.size();
    };
    for (unsigned char c : in) {
        if (c == '\n') { out.append("\r\n"); col = 0; continue; }
        if (c == '\r') continue;
        if ((c >= 33 && c <= 126 && c != '=') || c == ' ' || c == '\t') {
            emit(std::string(1, static_cast<char>(c)));
        } else {
            std::string e = "="; e.push_back(HEX[c >> 4]); e.push_back(HEX[c & 0xF]);
            emit(e);
        }
    }
    return out;
}

// ---- RFC 2047 --------------------------------------------------------------

// Q-encoding used inside encoded-words: '_' is space, "=XX" is a byte.
std::string DecodeQWord(const std::string& t) {
    std::string out;
    for (std::size_t i = 0; i < t.size(); ++i) {
        char c = t[i];
        if (c == '_') out.push_back(' ');
        else if (c == '=' && i + 2 < t.size()) {
            int hi = HexVal(t[i+1]), lo = HexVal(t[i+2]);
            if (hi >= 0 && lo >= 0) { out.push_back(static_cast<char>((hi << 4) | lo)); i += 2; }
            else out.push_back(c);
        } else out.push_back(c);
    }
    return out;
}

// Try to parse an encoded-word starting at s[i]; on success fill `out` (UTF-8)
// and set nextI past the "?=".
bool TryEncodedWord(const std::string& s, std::size_t i, std::string& out, std::size_t& nextI) {
    if (i + 1 >= s.size() || s[i] != '=' || s[i+1] != '?') return false;
    std::size_t cs = i + 2;
    std::size_t q1 = s.find('?', cs);
    if (q1 == std::string::npos) return false;
    if (q1 + 2 >= s.size() || s[q1+2] != '?') return false;
    char enc = static_cast<char>(std::toupper(static_cast<unsigned char>(s[q1+1])));
    std::size_t textStart = q1 + 3;
    std::size_t end = s.find("?=", textStart);
    if (end == std::string::npos) return false;

    std::string charset = s.substr(cs, q1 - cs);
    std::string text = s.substr(textStart, end - textStart);
    std::string rawBytes;
    if (enc == 'B') { std::vector<uint8_t> b; Base64DecodeImpl(text, b); rawBytes.assign(b.begin(), b.end()); }
    else if (enc == 'Q') { rawBytes = DecodeQWord(text); }
    else return false;

    out = CharsetToUtf8(rawBytes, charset);
    nextI = end + 2;
    return true;
}

std::string DecodeHeaderImpl(const std::string& raw) {
    std::string result;
    std::size_t i = 0, n = raw.size();
    bool lastWasWord = false;
    while (i < n) {
        std::string dec; std::size_t j;
        if (TryEncodedWord(raw, i, dec, j)) { result += dec; i = j; lastWasWord = true; continue; }
        if (lastWasWord && std::isspace(static_cast<unsigned char>(raw[i]))) {
            std::size_t k = i;
            while (k < n && std::isspace(static_cast<unsigned char>(raw[k]))) ++k;
            std::string dec2; std::size_t j2;
            if (k < n && TryEncodedWord(raw, k, dec2, j2)) { result += dec2; i = j2; continue; }
        }
        result.push_back(raw[i]); ++i; lastWasWord = false;
    }
    return result;
}

// ---- header / structure parsing --------------------------------------------

bool SplitHeadersBody(const std::string& raw, std::string& headers, std::string& body) {
    std::size_t p = raw.find("\r\n\r\n");
    if (p != std::string::npos) { headers = raw.substr(0, p); body = raw.substr(p + 4); return true; }
    p = raw.find("\n\n");
    if (p != std::string::npos) { headers = raw.substr(0, p); body = raw.substr(p + 2); return true; }
    headers = raw; body.clear();
    return false;
}

// Ordered header list (name, value) with folded lines joined.
std::vector<std::pair<std::string, std::string>> ParseHeaderList(const std::string& block) {
    std::vector<std::pair<std::string, std::string>> out;
    std::istringstream is(block);
    std::string line, curName, curVal;
    auto flush = [&]() { if (!curName.empty()) out.emplace_back(curName, Trim(curVal)); curName.clear(); curVal.clear(); };
    while (std::getline(is, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty() && (line[0] == ' ' || line[0] == '\t')) {
            curVal += ' '; curVal += Trim(line);            // folded continuation
        } else {
            flush();
            std::size_t colon = line.find(':');
            if (colon == std::string::npos) continue;
            curName = Trim(line.substr(0, colon));
            curVal  = Trim(line.substr(colon + 1));
        }
    }
    flush();
    return out;
}

std::string GetHeader(const std::vector<std::pair<std::string, std::string>>& hs,
                      const std::string& name) {
    std::string ln = Lower(name);
    for (const auto& h : hs) if (Lower(h.first) == ln) return h.second;
    return "";
}

// Parse "value; key=val; key2=\"val2\"" -> mediaType/value + params map.
void ParseValueWithParams(const std::string& header, std::string& value,
                          std::map<std::string, std::string>& params) {
    std::size_t semi = header.find(';');
    value = Lower(Trim(header.substr(0, semi)));
    if (semi == std::string::npos) return;
    std::string rest = header.substr(semi + 1);
    std::size_t i = 0;
    while (i < rest.size()) {
        std::size_t eq = rest.find('=', i);
        if (eq == std::string::npos) break;
        std::string key = Lower(Trim(rest.substr(i, eq - i)));
        std::size_t vstart = eq + 1;
        while (vstart < rest.size() && std::isspace(static_cast<unsigned char>(rest[vstart]))) ++vstart;
        std::string val;
        if (vstart < rest.size() && rest[vstart] == '"') {
            std::size_t vend = rest.find('"', vstart + 1);
            if (vend == std::string::npos) vend = rest.size();
            val = rest.substr(vstart + 1, vend - vstart - 1);
            i = vend + 1;
            std::size_t nsemi = rest.find(';', i);
            i = (nsemi == std::string::npos) ? rest.size() : nsemi + 1;
        } else {
            std::size_t vend = rest.find(';', vstart);
            if (vend == std::string::npos) vend = rest.size();
            val = Trim(rest.substr(vstart, vend - vstart));
            i = (vend == rest.size()) ? rest.size() : vend + 1;
        }
        if (!key.empty()) params[key] = val;
    }
}

std::vector<std::string> SplitMultipart(const std::string& body, const std::string& boundary) {
    std::vector<std::string> parts;
    const std::string d = "--" + boundary;
    std::size_t pos = body.find(d);
    if (pos == std::string::npos) return parts;
    pos += d.size();
    while (pos <= body.size()) {
        if (pos + 1 < body.size() && body[pos] == '-' && body[pos+1] == '-') break; // closing
        std::size_t lineEnd = body.find('\n', pos);
        if (lineEnd == std::string::npos) break;
        std::size_t partStart = lineEnd + 1;
        std::size_t next = body.find(d, partStart);
        if (next == std::string::npos) break;
        std::size_t partEnd = next;
        if (partEnd >= 2 && body.compare(partEnd - 2, 2, "\r\n") == 0) partEnd -= 2;
        else if (partEnd >= 1 && body[partEnd - 1] == '\n') partEnd -= 1;
        parts.push_back(body.substr(partStart, partEnd - partStart));
        pos = next + d.size();
    }
    return parts;
}

void ParsePart(const std::string& raw, UltraNetMimePart& part) {
    std::string headerBlock, body;
    SplitHeadersBody(raw, headerBlock, body);
    auto hs = ParseHeaderList(headerBlock);
    for (const auto& h : hs) part.headers[h.first] = h.second;

    std::string ct = GetHeader(hs, "content-type");
    std::map<std::string, std::string> ctParams;
    std::string mediaType;
    ParseValueWithParams(ct, mediaType, ctParams);
    if (mediaType.empty()) mediaType = "text/plain";
    part.mediaType = mediaType;
    part.charset = ctParams.count("charset") ? Lower(ctParams["charset"]) : "";
    part.transferEncoding = Lower(Trim(GetHeader(hs, "content-transfer-encoding")));

    std::string cd = GetHeader(hs, "content-disposition");
    std::map<std::string, std::string> cdParams;
    std::string disp;
    ParseValueWithParams(cd, disp, cdParams);
    part.disposition = disp;

    std::string filename;
    if (cdParams.count("filename*"))     filename = cdParams["filename*"];
    else if (cdParams.count("filename")) filename = cdParams["filename"];
    else if (ctParams.count("name"))     filename = ctParams["name"];
    part.filename = DecodeHeaderImpl(filename);

    std::string cid = Trim(GetHeader(hs, "content-id"));
    if (!cid.empty() && cid.front() == '<' && cid.back() == '>') cid = cid.substr(1, cid.size() - 2);
    part.contentId = cid;

    if (mediaType.rfind("multipart/", 0) == 0 && ctParams.count("boundary")) {
        part.isMultipart = true;
        for (const auto& sub : SplitMultipart(body, ctParams["boundary"])) {
            UltraNetMimePart child;
            ParsePart(sub, child);
            part.children.push_back(std::move(child));
        }
    } else {
        if (part.transferEncoding == "base64") {
            Base64DecodeImpl(body, part.body);
        } else if (part.transferEncoding == "quoted-printable") {
            std::string s = QpDecodeImpl(body);
            part.body.assign(s.begin(), s.end());
        } else {
            part.body.assign(body.begin(), body.end());
        }
    }
}

// ---- display body / attachment walking -------------------------------------

bool FindSpecificText(const UltraNetMimePart& part, const std::string& mt, std::string& out) {
    if (!part.isMultipart) {
        if (part.mediaType == mt) { out = CharsetToUtf8(std::string(part.body.begin(), part.body.end()), part.charset); return true; }
        return false;
    }
    for (const auto& c : part.children) if (FindSpecificText(c, mt, out)) return true;
    return false;
}

bool FindDisplay(const UltraNetMimePart& part, std::string& out, bool& isHtml) {
    if (!part.isMultipart) {
        if (part.mediaType == "text/html")  { out = CharsetToUtf8(std::string(part.body.begin(), part.body.end()), part.charset); isHtml = true;  return true; }
        if (part.mediaType == "text/plain") {
            // A downloadable .txt attachment is not the display body.
            if (part.disposition == "attachment") return false;
            out = CharsetToUtf8(std::string(part.body.begin(), part.body.end()), part.charset); isHtml = false; return true;
        }
        return false;
    }
    if (part.mediaType == "multipart/alternative") {
        if (FindSpecificText(part, "text/html",  out)) { isHtml = true;  return true; }
        if (FindSpecificText(part, "text/plain", out)) { isHtml = false; return true; }
    }
    for (const auto& c : part.children) if (FindDisplay(c, out, isHtml)) return true;
    return false;
}

void WalkAttachments(const UltraNetMimePart& part, std::vector<UltraNetMimeAttachmentView>& out,
                     bool includeInline) {
    if (part.isMultipart) {
        for (const auto& c : part.children) WalkAttachments(c, out, includeInline);
        return;
    }
    const bool isText = part.mediaType.rfind("text/", 0) == 0;
    bool isAttachment = (part.disposition == "attachment") ||
                        (!part.filename.empty() && !isText);
    bool isInline = (part.disposition == "inline") || (!part.contentId.empty() && !isText);
    if (isAttachment || (isInline && includeInline)) {
        UltraNetMimeAttachmentView v;
        v.filename = part.filename;
        v.mediaType = part.mediaType;
        v.contentId = part.contentId;
        v.isInline = isInline && !isAttachment;
        v.data = part.body;
        out.push_back(std::move(v));
    }
}

// ---- build helpers ---------------------------------------------------------

std::string CommaJoin(const std::vector<std::string>& v) {
    std::string out;
    for (std::size_t i = 0; i < v.size(); ++i) { if (i) out += ", "; out += v[i]; }
    return out;
}

std::string Rfc2822Date() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[64];
    std::strftime(buf, sizeof buf, "%a, %d %b %Y %H:%M:%S +0000", &tm);
    return buf;
}

std::string GenBoundary() {
    static std::atomic<uint64_t> counter{0};
    std::ostringstream os;
    os << "----=_UltraNet_"
       << std::chrono::system_clock::now().time_since_epoch().count()
       << "_" << counter.fetch_add(1);
    return os.str();
}

std::string GenMessageId() {
    std::random_device rd;
    std::ostringstream os;
    os << "<" << std::hex << rd() << rd() << "@ultranet.local>";
    return os.str();
}

bool ReservedHeader(const std::string& name) {
    std::string n = Lower(name);
    return n == "from" || n == "to" || n == "cc" || n == "bcc" || n == "subject" ||
           n == "date" || n == "message-id" || n == "mime-version" ||
           n == "content-type" || n == "content-transfer-encoding";
}

} // namespace

// ============================================================================
// Public API
// ============================================================================

std::string UltraNet_Base64Encode(const std::vector<uint8_t>& data, bool wrap76Cols) {
    return Base64EncodeImpl(data, wrap76Cols);
}
bool UltraNet_Base64Decode(const std::string& text, std::vector<uint8_t>& out) {
    return Base64DecodeImpl(text, out);
}
std::string UltraNet_QuotedPrintableEncode(const std::string& text) { return QpEncodeImpl(text); }
std::string UltraNet_QuotedPrintableDecode(const std::string& text) { return QpDecodeImpl(text); }

std::string UltraNet_MimeDecodeHeader(const std::string& raw) { return DecodeHeaderImpl(raw); }

std::string UltraNet_MimeEncodeHeader(const std::string& utf8Value, bool useBase64) {
    bool ascii = true;
    for (unsigned char c : utf8Value) if (c >= 0x80) { ascii = false; break; }
    if (ascii) return utf8Value;
    if (useBase64) {
        std::vector<uint8_t> b(utf8Value.begin(), utf8Value.end());
        return "=?UTF-8?B?" + Base64EncodeImpl(b, false) + "?=";
    }
    std::string q;
    for (unsigned char c : utf8Value) {
        if (c == ' ') q.push_back('_');
        else if ((c >= 33 && c <= 126) && c != '=' && c != '?' && c != '_')
            q.push_back(static_cast<char>(c));
        else { static const char* H = "0123456789ABCDEF"; q.push_back('='); q.push_back(H[c >> 4]); q.push_back(H[c & 0xF]); }
    }
    return "=?UTF-8?Q?" + q + "?=";
}

bool UltraNet_MimeParse(const std::string& rawMessage, UltraNetMimeMessage& out) {
    if (rawMessage.empty()) return false;
    out = UltraNetMimeMessage{};
    ParsePart(rawMessage, out.root);

    auto get = [&](const char* name) -> std::string {
        std::string ln = Lower(name);
        for (const auto& kv : out.root.headers) if (Lower(kv.first) == ln) return kv.second;
        return "";
    };
    out.subject   = DecodeHeaderImpl(get("subject"));
    out.from      = DecodeHeaderImpl(get("from"));
    out.date      = get("date");
    out.messageId = get("message-id");
    out.inReplyTo = get("in-reply-to");
    auto splitAddrs = [](const std::string& v, std::vector<std::string>& dst) {
        std::size_t i = 0;
        while (i < v.size()) {
            std::size_t comma = v.find(',', i);
            if (comma == std::string::npos) comma = v.size();
            std::string a = Trim(v.substr(i, comma - i));
            if (!a.empty()) dst.push_back(DecodeHeaderImpl(a));
            i = comma + 1;
        }
    };
    splitAddrs(get("to"), out.to);
    splitAddrs(get("cc"), out.cc);
    return true;
}

bool UltraNet_MimeGetDisplayBody(const UltraNetMimeMessage& message,
                                 std::string& outText, bool& outIsHtml) {
    outText.clear(); outIsHtml = false;
    return FindDisplay(message.root, outText, outIsHtml);
}

void UltraNet_MimeCollectAttachments(const UltraNetMimeMessage& message,
                                     std::vector<UltraNetMimeAttachmentView>& out,
                                     bool includeInline) {
    out.clear();
    WalkAttachments(message.root, out, includeInline);
}

std::string UltraNet_MimeBuild(const UltraNetMimeBuildInput& in) {
    std::ostringstream os;
    const bool hasAtt = !in.attachments.empty();
    const std::string boundary = in.boundary.empty() ? GenBoundary() : in.boundary;
    const std::string date = in.date.empty() ? Rfc2822Date() : in.date;
    const std::string msgId = in.messageId.empty() ? GenMessageId() : in.messageId;

    std::string bodyCt = in.bodyMediaType.empty() ? "text/plain" : in.bodyMediaType;
    if (bodyCt.rfind("text/", 0) == 0 && !in.bodyCharset.empty())
        bodyCt += "; charset=" + in.bodyCharset;

    os << "From: " << in.from << "\r\n";
    if (!in.to.empty()) os << "To: " << CommaJoin(in.to) << "\r\n";
    if (!in.cc.empty()) os << "Cc: " << CommaJoin(in.cc) << "\r\n";
    os << "Subject: " << UltraNet_MimeEncodeHeader(in.subject) << "\r\n"
       << "Date: " << date << "\r\n"
       << "Message-ID: " << msgId << "\r\n"
       << "MIME-Version: 1.0\r\n";

    for (const auto& [name, value] : in.extraHeaders)
        if (!ReservedHeader(name)) os << name << ": " << value << "\r\n";

    if (!hasAtt) {
        os << "Content-Type: " << bodyCt << "\r\n"
           << "Content-Transfer-Encoding: 8bit\r\n\r\n"
           << in.body;
        return os.str();
    }

    os << "Content-Type: multipart/mixed; boundary=\"" << boundary << "\"\r\n\r\n"
       << "This is a multi-part message in MIME format.\r\n"
       << "--" << boundary << "\r\n"
       << "Content-Type: " << bodyCt << "\r\n"
       << "Content-Transfer-Encoding: 8bit\r\n\r\n"
       << in.body << "\r\n";

    for (const auto& a : in.attachments) {
        const std::string ct = a.mediaType.empty() ? "application/octet-stream" : a.mediaType;
        os << "--" << boundary << "\r\n"
           << "Content-Type: " << ct << "; name=\"" << a.filename << "\"\r\n"
           << "Content-Disposition: " << (a.isInline ? "inline" : "attachment")
           << "; filename=\"" << a.filename << "\"\r\n";
        if (a.isInline && !a.contentId.empty())
            os << "Content-ID: <" << a.contentId << ">\r\n";
        os << "Content-Transfer-Encoding: base64\r\n\r\n"
           << Base64EncodeImpl(a.data, true);
    }
    os << "--" << boundary << "--\r\n";
    return os.str();
}
