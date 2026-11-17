// Plugins/UltraIM/xmpp/XmppStream.h
// XMPP - one XML stream over one WebSocket (RFC 7395): every frame one
// complete element (<open/>, <close/>, a SASL element, a stanza), parsed
// into an XmlElement and handed on.
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
// Version: 0.1.0
// Last Modified: 2026-11-17
// Author: UltraCanvas Framework
#pragma once

#include "XmppXml.h"

#include "UltraNet/UltraNetCore.h"

#include <functional>
#include <memory>
#include <string>

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
    bool IsOpen() const;
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
    std::shared_ptr<Shared> shared_;
};

// An iq's error as words ("item-not-found", the server's text when it gives one).
std::string IqErrorText(const XmlElement& iq);
inline bool IqOk(const XmlElement& iq) {
    return iq.Is("iq") && iq.Attr("type") == "result";
}

} // namespace UltraIMXmpp
