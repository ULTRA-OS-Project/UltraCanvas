// Plugins/UltraIM/xmpp/XmppTcp.h
// XMPP - the classic TCP binding (RFC 6120): what XmppStream needs to carry
// a stream on one byte stream instead of a WebSocket.
//
//   - XmppSplitter cuts the bytes that arrive into the server's stream
//     header and the top-level elements after it, one by one, however the
//     reads split them (an element over several reads, several in one);
//     CDATA, comments, quoted attribute values with '>' in them, empty
//     elements and nested elements of the same name are taken as they are.
//     A new header in the middle of a stream is the restart after SASL;
//     "</stream:stream>" its end.
//   - XmppTcpConnection is the TCP connection itself through UltraNet, with
//     TLS from the start (XEP-0368 direct TLS) or later (STARTTLS) through
//     UltraNet_TlsWrap / UltraNet_TlsHandshake - the certificate and the host
//     name always verified. Used from one thread only: TLS cannot read and
//     write from two threads at once, so the reads wait briefly and the
//     writes go between them. UltraNet tells a read that waited out its time
//     (Timeout: quiet) from a connection that closed or broke, and raises no
//     SIGPIPE on a write to a closed one.
//   - The endpoints: "xmpp://host:port" (STARTTLS), "xmpps://host:port"
//     (direct TLS), "wss://..." / "ws://..." (the WebSocket binding), and the
//     DNS SRV records that announce them (_xmpps-client._tcp,
//     _xmpp-client._tcp), put in the order RFC 2782 asks for.
//
// Version: 0.1.0
// Last Modified: 2026-11-17
// Author: UltraCanvas Framework
#pragma once

#include "XmppXml.h"

#include "UltraNet/UltraNetCore.h"
#include "UltraNet/UltraNetTls.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace UltraIMXmpp {

// ============================================================================
// SPLITTING THE STREAM
// ============================================================================

class XmppSplitter {
public:
    enum class Kind {
        Header,     // the server's stream header (its start tag), the first or a restart
        Element,    // one complete top-level element, as it came
        End,        // </stream:stream>
        Error       // not XML (text: why); nothing more comes out
    };
    struct Item {
        Kind kind = Kind::Element;
        std::string text;
    };

    // An element (or header) larger than this is not a stanza: Error.
    static constexpr size_t kMaxElement = 16 * 1024 * 1024;

    // Adds bytes; what they complete is appended to out.
    void Feed(const char* data, size_t size, std::vector<Item>& out);
    void Feed(const std::string& data, std::vector<Item>& out) { Feed(data.data(), data.size(), out); }
    // Forgets everything: a new stream from scratch (after STARTTLS).
    void Reset();
    // Bytes kept for an element not complete yet.
    size_t Pending() const { return buffer_.size(); }

private:
    std::string buffer_;        // from the open element's (or the next thing's) '<' on
    size_t scan_ = 0;           // where scanning goes on in buffer_
    int depth_ = 0;             // inside a top-level element: how deep
    bool inStream_ = false;     // after the header, before the end
    bool done_ = false;         // the end or an error came: nothing more
};

// One element as the splitter gave it, read in the namespaces the stream
// header declares ("stream:features" is features in Ns::Streams, a stanza
// without xmlns is in jabber:client).
bool XmppParseStreamElement(const std::string& header, const std::string& element, XmlElement& out,
                            std::string* error = nullptr);

// The stream header the client sends to open (and restart) a stream.
std::string XmppStreamHeader(const std::string& domain);

// ============================================================================
// THE CONNECTION
// ============================================================================

class XmppTcpConnection {
public:
    enum class ReadStatus {
        Data,       // bytes came
        Idle,       // nothing within the timeout
        Closed      // the connection is gone
    };

    XmppTcpConnection() = default;
    ~XmppTcpConnection() { Close(); }
    XmppTcpConnection(const XmppTcpConnection&) = delete;
    XmppTcpConnection& operator=(const XmppTcpConnection&) = delete;

    // Connects to host:port; with directTls shakes hands at once, verifying
    // the certificate for tlsHost (the XMPP domain). False with the reason in
    // error; certificateRefused when the certificate did not verify.
    bool Open(const std::string& host, int port, bool directTls, const std::string& tlsHost,
              const UltraNetTlsOptions& tlsOptions, int timeoutMs, std::string& error, bool& certificateRefused);
    // TLS on the open connection (STARTTLS, after the server's <proceed/>).
    bool StartTls(const std::string& tlsHost, const UltraNetTlsOptions& tlsOptions, std::string& error,
                  bool& certificateRefused);
    // Waits up to timeoutMs for bytes; appends them to data.
    ReadStatus Read(int timeoutMs, std::string& data);
    // All of it. False when the connection is gone.
    bool Send(const std::string& data);
    void Close();
    bool IsOpen() const { return handle_ != UltraNetInvalidHandle; }
    bool IsTls() const { return tls_; }

private:
    UltraNetHandle handle_ = UltraNetInvalidHandle;
    bool tls_ = false;
    int timeoutMs_ = 15000;
};

// ============================================================================
// ENDPOINTS
// ============================================================================

enum class XmppTransport {
    WebSocket,      // ws:// or wss:// (RFC 7395)
    StartTls,       // TCP, TLS after <starttls/> (RFC 6120), port 5222
    DirectTls       // TCP with TLS from the start (XEP-0368), port 5223
};

struct XmppEndpoint {
    XmppTransport transport = XmppTransport::StartTls;
    std::string host;           // lower case; an IPv6 address without brackets
    int port = 0;
    std::string url;            // the canonical form: kept, and given back by ParseXmppEndpoint
};

// "wss://...", "ws://...", "xmpp://host[:port]", "xmpps://host[:port]",
// "host:port", "host" (STARTTLS), "[v6]:port". False for anything else (an
// https:// address, another scheme, a port that is not one).
bool ParseXmppEndpoint(const std::string& text, XmppEndpoint& out);
// "xmpp://host:port" / "xmpps://host:port" (brackets around an IPv6 address).
std::string XmppEndpointUrl(XmppTransport transport, const std::string& host, int port);

// This computer: 127.0.0.0/8, ::1, localhost.
bool XmppIsLoopbackHost(const std::string& host);
// Whether a stream may go on without TLS to this host (no STARTTLS offered,
// a ws:// address): only to this computer, as the tests run.
bool XmppPlainStreamAllowed(const std::string& host);

// ---- DNS SRV ----------------------------------------------------------------

struct XmppSrvRecord {
    int priority = 0;
    int weight = 0;
    int port = 0;
    std::string target;         // without the trailing dot
    bool directTls = false;     // from _xmpps-client
};

// UltraNet_DnsResolve's SRV answers ("priority weight port target") read.
// A single record with the target "." means the service is not offered
// there: no records, and notOffered set.
std::vector<XmppSrvRecord> XmppParseSrvAnswers(const std::vector<std::string>& answers, bool directTls,
                                               bool* notOffered = nullptr);
// The order to try them in (RFC 2782): by priority, the lowest first; in one
// priority direct TLS before STARTTLS (XEP-0368), and each of those by a
// draw weighted by the records' weights. random(n) gives a number in
// [0, n] (n >= 0); empty: a real random one.
std::vector<XmppSrvRecord> XmppOrderSrvRecords(std::vector<XmppSrvRecord> records,
                                               const std::function<uint32_t(uint32_t)>& random = nullptr);
// The domain's announced endpoints, in the order to try them (both SRV
// lookups; nothing when DNS has none).
// startTlsRefused: _xmpp-client says "." (no STARTTLS on this domain).
std::vector<XmppEndpoint> XmppLookupSrvEndpoints(const std::string& domain, int timeoutMs,
                                                 bool* startTlsRefused = nullptr);

} // namespace UltraIMXmpp
