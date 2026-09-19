// Apps/UltraMail/engine/UltraMailSenderTrust.h
// Who a message is from, in one value: is the sender in the address book (and
// as what), which known service the address belongs to, and what the content
// scan made of the message. This is what the badge to the left of a subject
// line draws, and what its tooltip says.
//
// The rule the classification follows is "known beats guessed, danger beats
// known": an address in the address book is a contact even when the message is
// bulk, but a message whose links lie about where they go is called a scam
// even if the address book knows the sender — an address book entry is not
// evidence that *this* message is genuinely from them.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailContacts.h"
#include "UltraMailThreatScan.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace UltraMail {

class ContactStore;

// What the badge says about a sender. The order is the badge's own ranking —
// the user reads it as "safe … unknown … dangerous".
enum class SenderClass {
    Friend = 0,      // in the address book, a private section (green)
    Business,        // in the address book, Work / Services (blue)
    New,             // never seen in the address book (black outline)
    Advertisement,   // bulk / marketing mail (dark blue outline)
    Spam,            // spam markers, or a content scan that did not add up (orange)
    Scam             // phishing markers: links that lie about their target (red)
};

std::string ToString(SenderClass cls);          // stable identifier
std::string DisplayName(SenderClass cls);       // "Known contact", "Likely scam", …

// Lowercased address -> the section of the contact that carries it.
class ContactIndex {
public:
    void Clear() { byAddress_.clear(); }
    void Add(const std::string& address, ContactSection section);
    void Build(const std::vector<Contact>& contacts);

    // True when the address is in the book; `outSection` receives its section.
    bool Lookup(const std::string& address, ContactSection& outSection) const;
    bool Contains(const std::string& address) const {
        ContactSection ignored = ContactSection::Other;
        return Lookup(address, ignored);
    }
    std::size_t Size() const { return byAddress_.size(); }

private:
    std::map<std::string, ContactSection> byAddress_;
};

// Load every contact of every section into an index (one pass over the book).
bool BuildContactIndex(const ContactStore& store, ContactIndex& out);

// What the classifier is told about one message.
struct SenderIdentity {
    std::string address;         // envelope From address
    std::string displayName;     // From display name
    std::string subject;
    bool        junkFolder = false;          // it is sitting in Junk/Spam
    ThreatLevel level = ThreatLevel::Unscanned;   // the stored content-scan verdict
    bool        bulk  = false;               // the scan saw bulk/marketing markers
};

// The badge's content.
struct SenderStatus {
    SenderClass    cls           = SenderClass::New;
    bool           inAddressBook = false;
    ContactSection section       = ContactSection::Other;

    // The known service the address belongs to (empty when it is not one).
    std::string    brandId;
    std::string    brandName;
    uint32_t       brandAccentRgb = 0x5B6470;

    // One sentence for the tooltip; the scan's own reasons are appended by the
    // caller, which has them from the store.
    std::string    reason;

    bool Known() const { return cls == SenderClass::Friend || cls == SenderClass::Business; }
    bool Dangerous() const { return cls == SenderClass::Spam || cls == SenderClass::Scam; }
};

SenderStatus ClassifySender(const SenderIdentity& who, const ContactIndex& contacts);

} // namespace UltraMail
