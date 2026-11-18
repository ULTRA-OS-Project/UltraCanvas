// Plugins/UltraIM/xmpp/XmppTcp.cpp
// XMPP over TCP (RFC 6120): the stream cut into elements, the connection with
// direct TLS or STARTTLS, the endpoints and their DNS SRV records
// Version: 0.1.0
// Last Modified: 2026-11-17
// Author: UltraCanvas Framework

#include "XmppTcp.h"

#include "UltraNet/UltraNetDns.h"
#include "UltraNet/UltraNetSocket.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <random>
#include <sstream>

namespace UltraIMXmpp {

namespace {

bool IsSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

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

// Whether what the buffer holds from `at` on is the start of `pattern` (it
// may still become it), or already all of it.
bool MayBe(const std::string& buffer, size_t at, const char* pattern) {
    for (size_t i = 0; pattern[i]; ++i) {
        if (at + i >= buffer.size()) return true;
        if (buffer[at + i] != pattern[i]) return false;
    }
    return true;
}

bool Is(const std::string& buffer, size_t at, const char* pattern) {
    size_t i = 0;
    for (; pattern[i]; ++i) {
        if (at + i >= buffer.size() || buffer[at + i] != pattern[i]) return false;
    }
    return true;
}

enum class TagScan { Complete, Incomplete, Malformed };

// The '>' that ends the start tag at buffer[at] == '<' (a '>' inside a quoted
// attribute value does not).
TagScan StartTagEnd(const std::string& buffer, size_t at, size_t& end) {
    char quote = 0;
    for (size_t i = at + 1; i < buffer.size(); ++i) {
        const char c = buffer[i];
        if (quote) {
            if (c == quote) quote = 0;
        } else if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '>') {
            end = i;
            return TagScan::Complete;
        } else if (c == '<') {
            return TagScan::Malformed;
        }
    }
    return TagScan::Incomplete;
}

// The qualified name of the tag at buffer[at] == '<' ("stream:stream").
std::string TagName(const std::string& tag, size_t at = 0) {
    size_t i = at + 1;
    if (i < tag.size() && tag[i] == '/') ++i;
    const size_t begin = i;
    while (i < tag.size() && !IsSpace(tag[i]) && tag[i] != '/' && tag[i] != '>') ++i;
    return tag.substr(begin, i - begin);
}

std::string LocalName(const std::string& qname) {
    const size_t colon = qname.find(':');
    return colon == std::string::npos ? qname : qname.substr(colon + 1);
}

bool IsIpLiteral(const std::string& host) {
    if (host.find(':') != std::string::npos) return true;
    for (char c : host) {
        if (!(c == '.' || (c >= '0' && c <= '9'))) return false;
    }
    return !host.empty();
}

} // namespace

// ============================================================================
// SPLITTING THE STREAM
// ============================================================================

void XmppSplitter::Reset() {
    buffer_.clear();
    scan_ = 0;
    depth_ = 0;
    inStream_ = false;
    done_ = false;
}

void XmppSplitter::Feed(const char* data, size_t size, std::vector<Item>& out) {
    if (done_) return;
    buffer_.append(data, size);
    const auto fail = [&](const std::string& why) {
        done_ = true;
        buffer_.clear();
        scan_ = 0;
        depth_ = 0;
        out.push_back({Kind::Error, why});
    };
    // A construct not complete yet: wait for more, unless it is too big to be one.
    const auto wait = [&]() {
        if (buffer_.size() > kMaxElement) fail("an element larger than " + std::to_string(kMaxElement) + " bytes");
    };
    for (;;) {
        if (depth_ == 0) {
            // Between elements: whitespace (the keepalive), dropped.
            size_t p = 0;
            while (p < buffer_.size() && IsSpace(buffer_[p])) ++p;
            buffer_.erase(0, p);
            scan_ = 0;
            if (buffer_.empty()) return;
            if (buffer_[0] != '<') return fail("text outside of an element");
            if (buffer_.size() < 2) return;
            const char second = buffer_[1];
            if (second == '?') {
                // The XML declaration (or another processing instruction): passed over.
                const size_t end = buffer_.find("?>", 2);
                if (end == std::string::npos) return wait();
                buffer_.erase(0, end + 2);
                continue;
            }
            if (second == '!') {
                if (!MayBe(buffer_, 0, "<!--")) return fail("markup a stream does not allow (DTD, CDATA)");
                if (!Is(buffer_, 0, "<!--")) return;
                const size_t end = buffer_.find("-->", 4);
                if (end == std::string::npos) return wait();
                buffer_.erase(0, end + 3);
                continue;
            }
            if (second == '/') {
                const size_t end = buffer_.find('>');
                if (end == std::string::npos) return wait();
                if (!inStream_) return fail("an end tag before the stream began");
                // </stream:stream>: what follows it is no longer the stream's.
                inStream_ = false;
                done_ = true;
                buffer_.clear();
                out.push_back({Kind::End, std::string()});
                return;
            }
            size_t end = 0;
            const TagScan scan = StartTagEnd(buffer_, 0, end);
            if (scan == TagScan::Incomplete) return wait();
            if (scan == TagScan::Malformed) return fail("a tag that is not well-formed");
            const std::string local = LocalName(TagName(buffer_));
            const bool empty = buffer_[end - 1] == '/';
            if (!inStream_ || local == "stream") {
                // The header, or a new one (the restart after SASL).
                if (local != "stream") return fail("the stream did not begin with <stream:stream>");
                if (empty) {
                    done_ = true;
                    buffer_.clear();
                    out.push_back({Kind::End, std::string()});
                    return;
                }
                inStream_ = true;
                out.push_back({Kind::Header, buffer_.substr(0, end + 1)});
                buffer_.erase(0, end + 1);
                continue;
            }
            if (empty) {
                out.push_back({Kind::Element, buffer_.substr(0, end + 1)});
                buffer_.erase(0, end + 1);
                continue;
            }
            depth_ = 1;
            scan_ = end + 1;
            continue;
        }
        // In a top-level element: its end is where the depth comes back to 0.
        const size_t p = buffer_.find('<', scan_);
        if (p == std::string::npos) {
            scan_ = buffer_.size();
            return wait();
        }
        scan_ = p;
        if (p + 1 >= buffer_.size()) return wait();
        const char second = buffer_[p + 1];
        if (second == '/') {
            const size_t end = buffer_.find('>', p);
            if (end == std::string::npos) return wait();
            if (--depth_ == 0) {
                out.push_back({Kind::Element, buffer_.substr(0, end + 1)});
                buffer_.erase(0, end + 1);
                scan_ = 0;
            } else {
                scan_ = end + 1;
            }
            continue;
        }
        if (second == '?') {
            const size_t end = buffer_.find("?>", p + 2);
            if (end == std::string::npos) return wait();
            scan_ = end + 2;
            continue;
        }
        if (second == '!') {
            const bool comment = MayBe(buffer_, p, "<!--"), cdata = MayBe(buffer_, p, "<![CDATA[");
            if (!comment && !cdata) return fail("markup a stream does not allow (DTD)");
            if (comment && Is(buffer_, p, "<!--")) {
                const size_t end = buffer_.find("-->", p + 4);
                if (end == std::string::npos) return wait();
                scan_ = end + 3;
                continue;
            }
            if (cdata && Is(buffer_, p, "<![CDATA[")) {
                const size_t end = buffer_.find("]]>", p + 9);
                if (end == std::string::npos) return wait();
                scan_ = end + 3;
                continue;
            }
            return wait();      // "<!" or "<![CD": not known yet
        }
        size_t end = 0;
        const TagScan scan = StartTagEnd(buffer_, p, end);
        if (scan == TagScan::Incomplete) return wait();
        if (scan == TagScan::Malformed) return fail("a tag that is not well-formed");
        if (buffer_[end - 1] != '/') ++depth_;
        scan_ = end + 1;
    }
}

bool XmppParseStreamElement(const std::string& header, const std::string& element, XmlElement& out,
                            std::string* error) {
    static const std::string kDefault =
        "<stream:stream xmlns='jabber:client' xmlns:stream='http://etherx.jabber.org/streams'>";
    const std::string& open = header.empty() ? kDefault : header;
    // Inside the header's start tag, the element has the namespaces it declares.
    XmlElement stream;
    if (!XmlParse(open + element + "</" + TagName(open) + ">", stream, error)) return false;
    if (stream.children.empty()) {
        if (error) *error = "no element";
        return false;
    }
    out = std::move(stream.children.front());
    return true;
}

std::string XmppStreamHeader(const std::string& domain) {
    return "<?xml version='1.0'?><stream:stream xmlns='jabber:client' xmlns:stream='http://etherx.jabber.org/streams'"
           " to='" + XmlEscape(domain) + "' version='1.0' xml:lang='en'>";
}

// ============================================================================
// THE CONNECTION
// ============================================================================

bool XmppTcpConnection::Open(const std::string& host, int port, bool directTls, const std::string& tlsHost,
                             const UltraNetTlsOptions& tlsOptions, int timeoutMs, std::string& error,
                             bool& certificateRefused) {
    Close();
    certificateRefused = false;
    UltraNetSocketOptions options;
    options.connectTimeoutMs = timeoutMs;
    options.noDelay = true;
    options.keepAlive = true;
    // The system's SOCKS proxy where one is set (never for this machine).
    options.useProxySetting = true;
    UltraNetHandle handle = UltraNetInvalidHandle;
    const UltraNetResult opened = UltraNet_TcpOpen(host, port, options, handle);
    if (!opened || handle == UltraNetInvalidHandle) {
        error = opened.message.empty() ? "could not connect" : opened.message;
        return false;
    }
    // Bounds the handshake, and every write from now on.
    UltraNet_SocketSetTimeout(handle, timeoutMs, timeoutMs);
    handle_ = handle;
    timeoutMs_ = timeoutMs;
    tls_ = false;
    if (directTls && !StartTls(tlsHost, tlsOptions, error, certificateRefused)) {
        Close();
        return false;
    }
    return true;
}

bool XmppTcpConnection::StartTls(const std::string& tlsHost, const UltraNetTlsOptions& tlsOptions, std::string& error,
                                 bool& certificateRefused) {
    certificateRefused = false;
    if (handle_ == UltraNetInvalidHandle) {
        error = "the connection is gone";
        return false;
    }
    UltraNetTlsOptions settings = tlsOptions;
    // Never without verification: the server is the domain it says it is.
    settings.verifyPeer = true;
    settings.verifyHostname = true;
    if (settings.sniHostname.empty() && !IsIpLiteral(tlsHost)) settings.sniHostname = tlsHost;
    // (The handshake writes with the connection's timeout set by Open.)
    UltraNet_SocketSetTimeout(handle_, timeoutMs_, timeoutMs_);
    if (UltraNet_TlsWrap(handle_, tlsHost, settings) != handle_) {
        error = "could not start TLS";
        return false;
    }
    const UltraNetResult shaken = UltraNet_TlsHandshake(handle_);
    if (!shaken) {
        certificateRefused = shaken.code == UltraNetResultCode::TlsCertificateInvalid;
        error = certificateRefused ? "the server's certificate is not trusted for " + tlsHost + " (" + shaken.message + ")"
                                   : "the TLS handshake failed (" + shaken.message + ")";
        return false;
    }
    tls_ = true;
    return true;
}

XmppTcpConnection::ReadStatus XmppTcpConnection::Read(int timeoutMs, std::string& data) {
    if (handle_ == UltraNetInvalidHandle) return ReadStatus::Closed;
    if (timeoutMs < 1) timeoutMs = 1;
    std::vector<uint8_t> chunk;
    const UltraNetResult r = UltraNet_TcpReceive(handle_, chunk, 16384, timeoutMs);
    // Timeout: quiet, the connection still there. Any other error: broken
    // (a TLS peer gone without close_notify too). No bytes: closed.
    if (!r) return r.code == UltraNetResultCode::Timeout ? ReadStatus::Idle : ReadStatus::Closed;
    if (chunk.empty()) return ReadStatus::Closed;
    data.append(chunk.begin(), chunk.end());
    return ReadStatus::Data;
}

bool XmppTcpConnection::Send(const std::string& data) {
    if (handle_ == UltraNetInvalidHandle) return false;
    size_t at = 0;
    while (at < data.size()) {
        const std::vector<uint8_t> part(data.begin() + static_cast<std::ptrdiff_t>(at), data.end());
        int sent = 0;
        const UltraNetResult r = UltraNet_TcpSend(handle_, part, sent);
        if (!r || sent <= 0) return false;      // (UltraNet raises no SIGPIPE)
        at += static_cast<size_t>(sent);
    }
    return true;
}

void XmppTcpConnection::Close() {
    if (handle_ == UltraNetInvalidHandle) return;
    UltraNet_SocketClose(handle_);
    handle_ = UltraNetInvalidHandle;
    tls_ = false;
}

// ============================================================================
// ENDPOINTS
// ============================================================================

std::string XmppEndpointUrl(XmppTransport transport, const std::string& host, int port) {
    const std::string h = host.find(':') != std::string::npos ? "[" + host + "]" : host;
    return std::string(transport == XmppTransport::DirectTls ? "xmpps://" : "xmpp://") + h + ":" + std::to_string(port);
}

bool ParseXmppEndpoint(const std::string& text, XmppEndpoint& out) {
    const std::string t = Trim(text);
    const std::string lower = Lower(t);
    out = XmppEndpoint();
    if (lower.rfind("wss://", 0) == 0 || lower.rfind("ws://", 0) == 0) {
        const bool secure = lower[2] == 's';
        std::string rest = t.substr(secure ? 6 : 5);
        rest = rest.substr(0, rest.find_first_of("/?#"));
        if (rest.empty()) return false;
        out.transport = XmppTransport::WebSocket;
        out.port = secure ? 443 : 80;
        if (rest.front() == '[') {
            const size_t close = rest.find(']');
            if (close == std::string::npos) return false;
            out.host = Lower(rest.substr(1, close - 1));
            if (close + 1 < rest.size() && rest[close + 1] == ':') out.port = std::atoi(rest.c_str() + close + 2);
        } else {
            const size_t colon = rest.rfind(':');
            out.host = Lower(rest.substr(0, colon));
            if (colon != std::string::npos) out.port = std::atoi(rest.c_str() + colon + 1);
        }
        out.url = t;
        return !out.host.empty() && out.port > 0 && out.port < 65536;
    }
    std::string rest = t;
    out.transport = XmppTransport::StartTls;
    out.port = 5222;
    const size_t scheme = lower.find("://");
    if (scheme != std::string::npos) {
        const std::string name = lower.substr(0, scheme);
        if (name == "xmpps") {
            out.transport = XmppTransport::DirectTls;
            out.port = 5223;
        } else if (name != "xmpp") {
            return false;
        }
        rest = t.substr(scheme + 3);
    }
    while (!rest.empty() && rest.back() == '/') rest.pop_back();
    if (rest.empty()) return false;
    std::string portText;
    if (rest.front() == '[') {
        const size_t close = rest.find(']');
        if (close == std::string::npos) return false;
        out.host = rest.substr(1, close - 1);
        const std::string after = rest.substr(close + 1);
        if (!after.empty()) {
            if (after.front() != ':') return false;
            portText = after.substr(1);
        }
    } else if (std::count(rest.begin(), rest.end(), ':') == 1) {
        const size_t colon = rest.find(':');
        out.host = rest.substr(0, colon);
        portText = rest.substr(colon + 1);
    } else {
        out.host = rest;    // a name, or an IPv6 address without a port
    }
    out.host = Lower(out.host);
    if (out.host.empty()) return false;
    for (char c : out.host) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_' || c == ':' ||
              static_cast<unsigned char>(c) >= 0x80))
            return false;
    }
    if (!portText.empty() || (rest.front() != '[' && rest.back() == ':')) {
        if (portText.empty() || portText.size() > 5 ||
            !std::all_of(portText.begin(), portText.end(), [](char c) { return c >= '0' && c <= '9'; }))
            return false;
        out.port = std::atoi(portText.c_str());
        if (out.port <= 0 || out.port > 65535) return false;
    }
    out.url = XmppEndpointUrl(out.transport, out.host, out.port);
    return true;
}

bool XmppIsLoopbackHost(const std::string& host) {
    std::string h = Lower(host);
    if (!h.empty() && h.front() == '[' && h.back() == ']') h = h.substr(1, h.size() - 2);
    if (h == "localhost" || h == "::1") return true;
    if (h.rfind("127.", 0) != 0) return false;
    return std::all_of(h.begin(), h.end(), [](char c) { return c == '.' || (c >= '0' && c <= '9'); });
}

bool XmppPlainStreamAllowed(const std::string& host) {
    return XmppIsLoopbackHost(host);
}

// ---- DNS SRV ----------------------------------------------------------------

std::vector<XmppSrvRecord> XmppParseSrvAnswers(const std::vector<std::string>& answers, bool directTls,
                                               bool* notOffered) {
    std::vector<XmppSrvRecord> out;
    bool dot = false;
    for (const std::string& answer : answers) {
        std::istringstream in(answer);
        XmppSrvRecord r;
        if (!(in >> r.priority >> r.weight >> r.port >> r.target)) continue;
        while (!r.target.empty() && r.target.back() == '.') r.target.pop_back();
        if (r.target.empty()) {
            dot = true;
            continue;
        }
        if (r.port <= 0 || r.port > 65535 || r.priority < 0 || r.weight < 0) continue;
        r.target = Lower(r.target);
        r.directTls = directTls;
        out.push_back(r);
    }
    // RFC 2782: "." alone - the service is decidedly not available here.
    const bool none = dot && answers.size() == 1;
    if (notOffered) *notOffered = none;
    return out;
}

std::vector<XmppSrvRecord> XmppOrderSrvRecords(std::vector<XmppSrvRecord> records,
                                               const std::function<uint32_t(uint32_t)>& random) {
    const auto draw = [&random](uint32_t n) -> uint32_t {
        if (random) return std::min(random(n), n);
        static thread_local std::mt19937 engine{std::random_device{}()};
        return std::uniform_int_distribution<uint32_t>(0, n)(engine);
    };
    std::stable_sort(records.begin(), records.end(), [](const XmppSrvRecord& a, const XmppSrvRecord& b) {
        if (a.priority != b.priority) return a.priority < b.priority;
        return a.directTls && !b.directTls;
    });
    std::vector<XmppSrvRecord> out;
    out.reserve(records.size());
    for (size_t begin = 0; begin < records.size();) {
        size_t end = begin;
        while (end < records.size() && records[end].priority == records[begin].priority &&
               records[end].directTls == records[begin].directTls)
            ++end;
        // RFC 2782's selection: the weight-0 records first, then draw a number
        // in [0, sum] and take the first record whose running sum reaches it.
        std::vector<XmppSrvRecord> group(records.begin() + static_cast<std::ptrdiff_t>(begin),
                                         records.begin() + static_cast<std::ptrdiff_t>(end));
        std::stable_partition(group.begin(), group.end(), [](const XmppSrvRecord& r) { return r.weight == 0; });
        while (!group.empty()) {
            uint32_t sum = 0;
            for (const XmppSrvRecord& r : group) sum += static_cast<uint32_t>(r.weight);
            const uint32_t pick = draw(sum);
            uint32_t running = 0;
            size_t chosen = group.size() - 1;
            for (size_t i = 0; i < group.size(); ++i) {
                running += static_cast<uint32_t>(group[i].weight);
                if (running >= pick) {
                    chosen = i;
                    break;
                }
            }
            out.push_back(group[chosen]);
            group.erase(group.begin() + static_cast<std::ptrdiff_t>(chosen));
        }
        begin = end;
    }
    return out;
}

std::vector<XmppEndpoint> XmppLookupSrvEndpoints(const std::string& domain, int timeoutMs, bool* startTlsRefused) {
    std::vector<XmppSrvRecord> records;
    if (startTlsRefused) *startTlsRefused = false;
    for (const bool direct : {true, false}) {
        std::vector<std::string> answers;
        const std::string name = std::string(direct ? "_xmpps-client._tcp." : "_xmpp-client._tcp.") + domain;
        if (!UltraNet_DnsResolve(name, answers, UltraNetDnsType::SRV, timeoutMs)) continue;
        bool notOffered = false;
        for (const XmppSrvRecord& r : XmppParseSrvAnswers(answers, direct, &notOffered)) records.push_back(r);
        if (!direct && notOffered && startTlsRefused) *startTlsRefused = true;
    }
    std::vector<XmppEndpoint> out;
    for (const XmppSrvRecord& r : XmppOrderSrvRecords(records)) {
        XmppEndpoint e;
        e.transport = r.directTls ? XmppTransport::DirectTls : XmppTransport::StartTls;
        e.host = r.target;
        e.port = r.port;
        e.url = XmppEndpointUrl(e.transport, e.host, e.port);
        if (std::none_of(out.begin(), out.end(), [&e](const XmppEndpoint& o) { return o.url == e.url; }))
            out.push_back(e);
    }
    return out;
}

} // namespace UltraIMXmpp
