// Apps/UltraMail/engine/UltraMailContactCollector.cpp
// Version: 0.5.0 - a service only with its domain proven; NeverWrittenTo
// Version: 0.4.0 - incoming mail adds only known services, and never a
//                  blocked or forgotten address; Collect reads "Name <address>"
// Version: 0.3.0 - CollectSenders: a batch in one read and one transaction
// Version: 0.2.0 - known services are collected as business contacts
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailContactCollector.h"

#include "UltraMailRecipientComplete.h"   // RecipientAddress
#include "UltraMailSenderBrands.h"
#include "UltraMailThreatScan.h"   // GetSenderLists

#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <vector>

namespace UltraMail {

namespace {
std::string Lower(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return r;
}
} // namespace

namespace {

// Shared by both entry points: the contact, or nothing when that address is
// already in the book.
bool SaveIfNew(ContactStore& store, const Contact& contact, const std::string& email) {
    const std::string target = Lower(email);
    std::vector<Contact> hits;
    if (store.Search(email, hits)) {
        for (const auto& c : hits)
            for (const auto& e : c.emails)
                if (Lower(e.address) == target) return false;
    }
    Contact copy = contact;
    return store.Save(copy).success;
}

// The contact CollectSender files for one sender in the known-sender
// registry: under Services with the service's name as the organization.
Contact ContactForService(const SenderBrand& brand, const std::string& name,
                          const std::string& email) {
    Contact c;
    ContactEmail e; e.address = email; e.primary = true; e.label = "work";
    // The service's own name is the more useful label for a contact whose
    // display name is a no-reply robot ("Kickstarter" beats "notifications").
    c.displayName  = name.empty() ? brand.name : name;
    c.organization = brand.name;
    c.section      = ContactSection::Services;
    c.notes        = DisplayName(brand.category) +
                     " — collected automatically from mail from " + brand.name + ".";
    c.emails.push_back(e);
    return c;
}

// The registry's entry for a sender incoming mail may add, or null: only a
// known service whose domain the receiving server proved, and not one the
// user blocked. (Forgotten addresses are checked by the callers, who have the
// list.)
const SenderBrand* CollectableService(const std::string& email,
                                      const std::string& verifiedDomain) {
    if (email.empty() || !DomainProven(email, verifiedDomain)) return nullptr;
    const SenderBrand* brand = BrandForAddress(email);
    if (!brand) return nullptr;
    if (!GetSenderLists().BlockedBy(email).empty()) return nullptr;
    return brand;
}

} // namespace

bool DomainProven(const std::string& address, const std::string& verifiedDomain) {
    const std::size_t at = address.rfind('@');
    if (at == std::string::npos || verifiedDomain.empty()) return false;
    std::string host = Lower(address.substr(at + 1));
    std::string proven = Lower(verifiedDomain);
    while (!host.empty() && host.back() == '>') host.pop_back();
    if (host.empty()) return false;
    // `inner` is `outer` or a subdomain of it.
    auto within = [](const std::string& inner, const std::string& outer) {
        return inner == outer ||
               (inner.size() > outer.size() &&
                inner.compare(inner.size() - outer.size(), outer.size(), outer) == 0 &&
                inner[inner.size() - outer.size() - 1] == '.');
    };
    // A parent counts only below the top level: "com" vouches for nothing.
    const bool provenHasDot = proven.find('.') != std::string::npos;
    const bool hostHasDot = host.find('.') != std::string::npos;
    return (provenHasDot && within(host, proven)) || (hostHasDot && within(proven, host));
}

bool ContactCollector::Collect(ContactStore& store, const std::string& name,
                               const std::string& recipient, ContactSection section) {
    // A recipient as the compose fields hold it: "Anna Schmidt <anna@...>"
    // (what a picked suggestion writes) or the bare address. Stored whole, the
    // address was "Anna Schmidt <anna@...>" - a second, broken contact.
    const std::string email = RecipientAddress(recipient);
    if (email.empty()) return false;
    std::string given = name;
    if (given.empty()) {
        const std::size_t lt = recipient.rfind('<');
        if (lt != std::string::npos) {
            given = recipient.substr(0, lt);
            while (!given.empty() && (std::isspace(static_cast<unsigned char>(given.back())) ||
                                      given.back() == '"'))
                given.pop_back();
            std::size_t b = 0;
            while (b < given.size() && (std::isspace(static_cast<unsigned char>(given[b])) ||
                                        given[b] == '"'))
                ++b;
            given = given.substr(b);
        }
    }

    Contact c;
    c.displayName = given.empty() ? email : given;
    c.section = section;
    ContactEmail e; e.address = email; e.primary = true;
    c.emails.push_back(e);
    return SaveIfNew(store, c, email);
}

bool ContactCollector::CollectSender(ContactStore& store, const std::string& name,
                                     const std::string& email, const std::string& verifiedDomain) {
    const SenderBrand* brand = CollectableService(email, verifiedDomain);
    if (!brand) return false;
    std::set<std::string> forgotten;
    store.ForgottenAddresses(forgotten);
    if (forgotten.count(Lower(email))) return false;
    return SaveIfNew(store, ContactForService(*brand, name, email), email);
}

int ContactCollector::CollectSenders(ContactStore& store, const std::vector<SenderSeen>& senders) {
    if (senders.empty()) return 0;
    std::set<std::string> known;
    if (!store.KnownAddresses(known)) return 0;
    std::set<std::string> forgotten;
    store.ForgottenAddresses(forgotten);
    std::vector<Contact> fresh;
    for (const SenderSeen& seen : senders) {
        const SenderBrand* brand = CollectableService(seen.address, seen.verifiedDomain);
        if (!brand) continue;
        const std::string key = Lower(seen.address);
        if (forgotten.count(key)) continue;
        // In the address book already, or earlier in this batch.
        if (!known.insert(key).second) continue;
        fresh.push_back(ContactForService(*brand, seen.name, seen.address));
    }
    if (fresh.empty()) return 0;
    return store.SaveAll(fresh) ? static_cast<int>(fresh.size()) : 0;
}

std::vector<Contact> ContactCollector::NeverWrittenTo(
        const std::vector<Contact>& contacts, const std::map<std::string, double>& writtenTo) {
    std::vector<Contact> out;
    for (const Contact& c : contacts) {
        if (c.section != ContactSection::Other || !c.group.empty()) continue;
        if (c.emails.size() != 1 || !c.phones.empty()) continue;
        if (!c.organization.empty() || !c.notes.empty()) continue;
        if (writtenTo.count(Lower(c.emails.front().address))) continue;
        out.push_back(c);
    }
    return out;
}

} // namespace UltraMail
