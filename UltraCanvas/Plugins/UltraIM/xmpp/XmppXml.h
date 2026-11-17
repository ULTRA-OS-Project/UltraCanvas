// Plugins/UltraIM/xmpp/XmppXml.h
// XMPP - the XML of a stanza as a small tree (parsed with tinyxml2, written
// with every text and attribute escaped), and the JID's parts.
//
// An XmlElement owns its copy of everything, so a stanza parsed on the
// WebSocket's thread can be handed to the provider's worker as it is. Names
// are local names with their namespace resolved: "<stream:features
// xmlns:stream='http://etherx.jabber.org/streams'>" is name "features",
// ns "http://etherx.jabber.org/streams"; a child without a namespace of
// its own has its parent's. Written back, an element says xmlns="..." only
// where its namespace differs from its parent's - never a prefix.
//
// Version: 0.1.0
// Last Modified: 2026-11-17
// Author: UltraCanvas Framework
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace UltraIMXmpp {

// The namespaces this provider speaks.
namespace Ns {
inline constexpr const char* Framing = "urn:ietf:params:xml:ns:xmpp-framing";
inline constexpr const char* Streams = "http://etherx.jabber.org/streams";
inline constexpr const char* Client = "jabber:client";
inline constexpr const char* Sasl = "urn:ietf:params:xml:ns:xmpp-sasl";
inline constexpr const char* Bind = "urn:ietf:params:xml:ns:xmpp-bind";
inline constexpr const char* Session = "urn:ietf:params:xml:ns:xmpp-session";
inline constexpr const char* Stanzas = "urn:ietf:params:xml:ns:xmpp-stanzas";
inline constexpr const char* Roster = "jabber:iq:roster";
inline constexpr const char* DiscoInfo = "http://jabber.org/protocol/disco#info";
inline constexpr const char* DiscoItems = "http://jabber.org/protocol/disco#items";
inline constexpr const char* Carbons = "urn:xmpp:carbons:2";
inline constexpr const char* Forward = "urn:xmpp:forward:0";
inline constexpr const char* Delay = "urn:xmpp:delay";
inline constexpr const char* Sid = "urn:xmpp:sid:0";
inline constexpr const char* Receipts = "urn:xmpp:receipts";
inline constexpr const char* Markers = "urn:xmpp:chat-markers:0";
inline constexpr const char* ChatStates = "http://jabber.org/protocol/chatstates";
inline constexpr const char* Correct = "urn:xmpp:message-correct:0";
inline constexpr const char* Retract = "urn:xmpp:message-retract:1";
inline constexpr const char* RetractOld = "urn:xmpp:message-retract:0";
inline constexpr const char* Fasten = "urn:xmpp:fasten:0";
inline constexpr const char* Reply = "urn:xmpp:reply:0";
inline constexpr const char* Fallback = "urn:xmpp:fallback:0";
inline constexpr const char* Hints = "urn:xmpp:hints";
inline constexpr const char* Mam = "urn:xmpp:mam:2";
inline constexpr const char* Rsm = "http://jabber.org/protocol/rsm";
inline constexpr const char* Data = "jabber:x:data";
inline constexpr const char* Oob = "jabber:x:oob";
inline constexpr const char* Upload = "urn:xmpp:http:upload:0";
inline constexpr const char* Muc = "http://jabber.org/protocol/muc";
inline constexpr const char* MucUser = "http://jabber.org/protocol/muc#user";
inline constexpr const char* MucOwner = "http://jabber.org/protocol/muc#owner";
inline constexpr const char* MucAdmin = "http://jabber.org/protocol/muc#admin";
inline constexpr const char* Conference = "jabber:x:conference";
inline constexpr const char* PubSub = "http://jabber.org/protocol/pubsub";
inline constexpr const char* PubSubEvent = "http://jabber.org/protocol/pubsub#event";
inline constexpr const char* Bookmarks = "urn:xmpp:bookmarks:1";
inline constexpr const char* Private = "jabber:iq:private";
inline constexpr const char* BookmarksLegacy = "storage:bookmarks";
inline constexpr const char* Ping = "urn:xmpp:ping";
} // namespace Ns

struct XmlElement {
    std::string name;       // the local name
    std::string ns;         // its namespace, resolved
    std::vector<std::pair<std::string, std::string>> attrs;   // without the xmlns declarations
    std::string text;       // its text (all of it, joined)
    std::vector<XmlElement> children;

    XmlElement() = default;
    explicit XmlElement(std::string localName, std::string nameSpace = std::string())
        : name(std::move(localName)), ns(std::move(nameSpace)) {}

    bool Is(const std::string& localName, const std::string& nameSpace = std::string()) const {
        return name == localName && (nameSpace.empty() || ns == nameSpace);
    }
    std::string Attr(const std::string& key) const;
    bool HasAttr(const std::string& key) const;
    // Sets (or replaces) an attribute; returns this element, to chain.
    XmlElement& Set(const std::string& key, const std::string& value);
    XmlElement& SetText(const std::string& value) {
        text = value;
        return *this;
    }

    // The first child with this name (and namespace; "" = any), or nullptr.
    const XmlElement* Child(const std::string& localName, const std::string& nameSpace = std::string()) const;
    XmlElement* Child(const std::string& localName, const std::string& nameSpace = std::string());
    std::vector<const XmlElement*> Children(const std::string& localName,
                                            const std::string& nameSpace = std::string()) const;
    // The text of the first such child ("" when there is none).
    std::string ChildText(const std::string& localName, const std::string& nameSpace = std::string()) const;
    // A new child (in this element's namespace when none is given); returns it.
    XmlElement& Add(const std::string& localName, const std::string& nameSpace = std::string());
    XmlElement& Add(XmlElement child);

    // The element as XML, every text and attribute value escaped.
    std::string Serialize(const std::string& parentNs = std::string()) const;
};

// One element out of a complete document (a WebSocket frame). False when
// it is not well-formed XML.
bool XmlParse(const std::string& text, XmlElement& out, std::string* error = nullptr);

// & < > " ' as entities, and the characters XML 1.0 does not allow left out.
std::string XmlEscape(const std::string& text);

// ---- JIDs ------------------------------------------------------------------
// "erin@example.org/phone": local "erin", domain "example.org", resource
// "phone"; the bare JID is "erin@example.org". The local part and the domain
// are compared without regard to case (lower-cased here; the resource is kept).
std::string JidBare(const std::string& jid);
std::string JidResource(const std::string& jid);
std::string JidLocal(const std::string& jid);
std::string JidDomain(const std::string& jid);
// A JID typed or pasted: "xmpp:" and "?join" taken off, spaces trimmed,
// local part and domain lower-cased; "" when it is not one.
std::string JidNormalize(const std::string& address);

// ---- Small things ----------------------------------------------------------
std::string Base64(const std::vector<uint8_t>& bytes);
std::string Base64(const std::string& bytes);
std::vector<uint8_t> Unbase64(const std::string& text);
// A random id of letters and digits.
std::string RandomId(size_t length = 16);
// XEP-0082 "2026-11-17T10:00:00Z" (fractions and offsets read) <-> ms since the epoch.
int64_t ParseXmppTime(const std::string& stamp);
std::string FormatXmppTime(int64_t ms);
int64_t NowMs();
// The UTF-8 text without its code points [from, to) - XEP-0428 fallback
// ranges count code points, not bytes.
std::string Utf8Erase(const std::string& text, size_t fromCodePoint, size_t toCodePoint);

} // namespace UltraIMXmpp
