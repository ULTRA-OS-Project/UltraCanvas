// Plugins/UltraIM/xmpp/XmppStream.h
// XMPP - one XML stream, over one of two transports: a WebSocket (RFC 7395),
// every frame one complete element (<open/>, <close/>, a SASL element, a
// stanza); or TCP (RFC 6120), the stream header and then the elements one
// after another on one byte stream, cut apart by XmppSplitter (XmppTcp.h),
// with TLS from the start (XEP-0368) or after STARTTLS. Either way each
// element is parsed into an XmlElement and handed on, and the provider
// speaks to both alike (OpenStream, Send, Exchange, Iq).
//
// Over TCP one thread of the stream's own does all the reading and writing
// (TLS cannot read and write from two threads at once): Send queues, and the
// thread writes between reads that wait briefly. A silent connection gets a
// single space now and then (RFC 6120's whitespace keepalive), which also
// tells a connection that is gone.
//
// UltraNet's WebSocket callbacks are process-wide: the first stream installs
// a router that takes the frames of the sockets that are streams' and hands
// every other socket's events to the callbacks that were installed before it
// (another provider's, the application's) - as ChatCube Direct's relay
// client does.
//
// The elements arrive on the socket's own thread. Each goes to onElement
// (which must not wait on this stream: it runs on the thread that delivers
// the answer) and to whoever waits for it (Exchange, Iq: the provider's
// worker, which may wait). An iq result or error a waiter asked for is the
// waiter's alone.
//
// Version: 0.3.0 - StartPinging: TCP asks a silent server (XEP-0199)
// Last Modified: 2026-11-17
// Version: 0.2.0 - the TCP binding (RFC 6120): ConnectTcp, STARTTLS, direct TLS, OpenStream
// Version: 0.1.0
// Author: UltraCanvas Framework
#pragma once

#include "XmppXml.h"

#include "UltraNet/UltraNetCore.h"
#include "UltraNet/UltraNetTls.h"

#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace UltraIMXmpp {

class XmppStream {
public:
    struct Shared;      // what the router reaches; outlives the stream while a callback runs

    XmppStream();
    ~XmppStream();
    XmppStream(const XmppStream&) = delete;
    XmppStream& operator=(const XmppStream&) = delete;

    // Set before Connect. onElement: every element, on the socket's thread.
    // onDropped: once, when the socket or the stream ended without Close()
    // (the server closed it, the network dropped) - also on that thread.
    std::function<void(const XmlElement&)> onElement;
    std::function<void(const std::string& why)> onDropped;

    // Opens the WebSocket (subprotocol "xmpp"); pingIntervalMs keeps a silent
    // connection checked (0: never).
    bool Connect(const std::string& url, int connectTimeoutMs, int pingIntervalMs, std::string& error);
    // Opens a TCP connection to host:port, with TLS at once when directTls
    // (XEP-0368). tlsHost: the name the server's certificate must be for (the
    // XMPP domain; always verified). keepaliveMs: a single space after that
    // long without sending anything (0: never). certificateRefused when the
    // certificate did not verify.
    bool ConnectTcp(const std::string& host, int port, bool directTls, const std::string& tlsHost,
                    const UltraNetTlsOptions& tlsOptions, int connectTimeoutMs, int keepaliveMs, std::string& error,
                    bool& certificateRefused);
    // Opens the stream, and opens it again after STARTTLS and SASL: <open/>
    // over a WebSocket, the stream header over TCP. The server's features.
    bool OpenStream(const std::string& domain, XmlElement& features, int timeoutMs);
    // TCP only: <starttls/>, and on the server's <proceed/> TLS on the
    // connection (the certificate verified for tlsHost). OpenStream next.
    bool StartTls(const std::string& tlsHost, const UltraNetTlsOptions& tlsOptions, int timeoutMs, std::string& error,
                  bool& certificateRefused);
    bool IsOpen() const;
    bool IsTcp() const;
    // TLS under the stream: wss://, direct TLS, or STARTTLS done.
    bool IsTls() const;
    // TCP, once signed in: after keepaliveMs with nothing from the server, a
    // ping (XEP-0199) to `to`; no answer - nor anything else - within
    // timeoutMs ends the stream (onDropped). Its answers are not handed on.
    // Nothing on WebSocket, which has its own pings.
    void StartPinging(const std::string& to, int timeoutMs);
    bool Send(const XmlElement& element);
    // Sends `element` and waits for the first element `match` accepts; false
    // on a timeout or the stream's end.
    bool Exchange(const XmlElement& element, const std::function<bool(const XmlElement&)>& match, XmlElement& reply,
                  int timeoutMs);
    // An iq (its id set here when it has none): its result or error.
    bool Iq(XmlElement iq, XmlElement& reply, int timeoutMs);
    // Ends the stream (<close/>) and the socket; onDropped is not called.
    // Not from onElement.
    void Close();

private:
    // The element as it goes on the wire (its namespace said where needed).
    std::string Serialized(const XmlElement& element) const;

    std::shared_ptr<Shared> shared_;
    std::thread io_;            // TCP: the connection's thread
};

// An iq's error as words ("item-not-found", the server's text when it gives one).
std::string IqErrorText(const XmlElement& iq);
inline bool IqOk(const XmlElement& iq) {
    return iq.Is("iq") && iq.Attr("type") == "result";
}

} // namespace UltraIMXmpp
