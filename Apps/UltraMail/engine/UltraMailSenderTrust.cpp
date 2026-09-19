// Apps/UltraMail/engine/UltraMailSenderTrust.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailSenderTrust.h"

#include "UltraMailContactStore.h"
#include "UltraMailSenderBrands.h"

#include <algorithm>
#include <cctype>

namespace UltraMail {

namespace {

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// The bare address of "Erika Example <erika@example.com>".
std::string BareAddress(const std::string& address) {
    std::string a = address;
    const std::size_t lt = a.find('<');
    if (lt != std::string::npos) {
        const std::size_t gt = a.find('>', lt);
        a = a.substr(lt + 1, gt == std::string::npos ? std::string::npos : gt - lt - 1);
    }
    // Trim
    std::size_t b = 0, e = a.size();
    while (b < e && std::isspace(static_cast<unsigned char>(a[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(a[e - 1]))) --e;
    return Lower(a.substr(b, e - b));
}

// Family, Friends and Leisure are the private sections; Work and Services are
// the ones a business relationship lands in. Other (auto-collected senders) is
// deliberately *not* a private contact — it means "we have corresponded", not
// "I vouch for them", so it reads as a business contact rather than a friend.
bool IsPrivateSection(ContactSection section) {
    return section == ContactSection::Family || section == ContactSection::Friends ||
           section == ContactSection::Leisure;
}

} // namespace

std::string ToString(SenderClass cls) {
    switch (cls) {
        case SenderClass::Friend:        return "friend";
        case SenderClass::Business:      return "business";
        case SenderClass::New:           return "new";
        case SenderClass::Advertisement: return "advertisement";
        case SenderClass::Spam:          return "spam";
        case SenderClass::Scam:          return "scam";
    }
    return "new";
}

std::string DisplayName(SenderClass cls) {
    switch (cls) {
        case SenderClass::Friend:        return "Known contact";
        case SenderClass::Business:      return "Business contact";
        case SenderClass::New:           return "New sender";
        case SenderClass::Advertisement: return "Likely advertisement";
        case SenderClass::Spam:          return "Likely spam";
        case SenderClass::Scam:          return "Likely scam";
    }
    return "New sender";
}

void ContactIndex::Add(const std::string& address, ContactSection section) {
    const std::string key = BareAddress(address);
    if (key.empty()) return;
    auto it = byAddress_.find(key);
    // A private section wins over a business one when the same address is in
    // two contacts: the badge should not demote someone's friend.
    if (it == byAddress_.end() || (IsPrivateSection(section) && !IsPrivateSection(it->second)))
        byAddress_[key] = section;
}

void ContactIndex::Build(const std::vector<Contact>& contacts) {
    Clear();
    for (const auto& c : contacts)
        for (const auto& e : c.emails) Add(e.address, c.section);
}

bool ContactIndex::Lookup(const std::string& address, ContactSection& outSection) const {
    const auto it = byAddress_.find(BareAddress(address));
    if (it == byAddress_.end()) return false;
    outSection = it->second;
    return true;
}

bool BuildContactIndex(const ContactStore& store, ContactIndex& out) {
    out.Clear();
    if (!store.IsOpen()) return false;
    bool ok = true;
    for (ContactSection section : { ContactSection::Family, ContactSection::Friends,
                                    ContactSection::Work, ContactSection::Leisure,
                                    ContactSection::Services, ContactSection::Other }) {
        std::vector<Contact> contacts;
        if (!store.ListBySection(section, contacts)) { ok = false; continue; }
        for (const auto& c : contacts)
            for (const auto& e : c.emails) out.Add(e.address, c.section);
    }
    return ok;
}

SenderStatus ClassifySender(const SenderIdentity& who, const ContactIndex& contacts) {
    SenderStatus status;

    if (const SenderBrand* brand = BrandForAddress(who.address)) {
        status.brandId        = brand->id;
        status.brandName      = brand->name;
        status.brandAccentRgb = brand->accentRgb;
        status.brandCategory  = brand->category;
        status.knownService   = true;
    }

    ContactSection section = ContactSection::Other;
    status.inAddressBook = contacts.Lookup(who.address, section);
    status.section       = section;

    // Danger first: a message whose links lie is a scam whoever it claims to be
    // from — an address book entry says nothing about *this* message.
    if (who.level == ThreatLevel::Scam) {
        status.cls    = SenderClass::Scam;
        status.reason = "This message shows phishing markers — check the reasons below "
                        "before clicking anything in it.";
        return status;
    }
    if (who.level == ThreatLevel::Suspicious || (who.junkFolder && !status.inAddressBook)) {
        status.cls    = SenderClass::Spam;
        status.reason = who.junkFolder && who.level != ThreatLevel::Suspicious
            ? "This message is in the junk folder."
            : "Parts of this message do not add up — treat its links with care.";
        return status;
    }

    if (status.inAddressBook) {
        status.cls = IsPrivateSection(section) ? SenderClass::Friend : SenderClass::Business;
        status.reason = "In your address book (" + DisplayName(section) + ").";
        return status;
    }

    // Bulk mail is called what it is even when it comes from a known service:
    // a campaign newsletter really is advertising, and saying so is the point
    // of the dark-blue badge.
    if (who.level == ThreatLevel::Advertisement || who.bulk) {
        status.cls = SenderClass::Advertisement;
        status.reason = status.brandName.empty()
            ? "Bulk mail — this looks like advertising or a newsletter."
            : "Bulk mail from " + status.brandName + " — a newsletter or an offer.";
        return status;
    }

    // A service in the known-sender registry is a business relationship in its
    // own right — the crowdfunding platform a project was backed on, the shop
    // an order came from — so it reads as a business contact even before the
    // address book has caught up. (ContactCollector files it there too, which
    // is what makes the registry a source of new business contacts rather than
    // only a source of icons.)
    if (status.knownService) {
        status.cls    = SenderClass::Business;
        status.reason = status.brandName + " \xE2\x80\x94 " +
                        DisplayName(status.brandCategory) +
                        ", not yet in your address book.";
        return status;
    }

    status.cls = SenderClass::New;
    status.reason = "New sender — this address is not in your address book.";
    return status;
}

} // namespace UltraMail
