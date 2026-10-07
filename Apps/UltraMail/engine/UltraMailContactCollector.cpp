// Apps/UltraMail/engine/UltraMailContactCollector.cpp
// Version: 0.3.0 - CollectSenders: a batch in one read and one transaction
// Version: 0.2.0 - known services are collected as business contacts
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailContactCollector.h"

#include "UltraMailSenderBrands.h"

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

// The contact CollectSender files for one sender: a known service under
// Services with the service's name as the organization, anything else in
// Other under the name it sent with (its address when it gave none).
Contact ContactForSender(const std::string& name, const std::string& email) {
    Contact c;
    ContactEmail e; e.address = email; e.primary = true;
    if (const SenderBrand* brand = BrandForAddress(email)) {
        // The service's own name is the more useful label for a contact whose
        // display name is a no-reply robot ("Kickstarter" beats "notifications").
        c.displayName  = name.empty() ? brand->name : name;
        c.organization = brand->name;
        c.section      = ContactSection::Services;
        c.notes        = DisplayName(brand->category) +
                         " — collected automatically from mail from " + brand->name + ".";
        e.label = "work";
    } else {
        c.displayName = name.empty() ? email : name;
        c.section     = ContactSection::Other;
    }
    c.emails.push_back(e);
    return c;
}

} // namespace

bool ContactCollector::Collect(ContactStore& store, const std::string& name,
                               const std::string& email, ContactSection section) {
    if (email.empty()) return false;

    Contact c;
    c.displayName = name.empty() ? email : name;
    c.section = section;
    ContactEmail e; e.address = email; e.primary = true;
    c.emails.push_back(e);
    return SaveIfNew(store, c, email);
}

bool ContactCollector::CollectSender(ContactStore& store, const std::string& name,
                                     const std::string& email) {
    if (email.empty()) return false;

    return SaveIfNew(store, ContactForSender(name, email), email);
}

int ContactCollector::CollectSenders(
        ContactStore& store, const std::vector<std::pair<std::string, std::string>>& senders) {
    if (senders.empty()) return 0;
    std::set<std::string> known;
    if (!store.KnownAddresses(known)) return 0;
    std::vector<Contact> fresh;
    for (const auto& [name, email] : senders) {
        if (email.empty()) continue;
        // In the address book already, or earlier in this batch.
        if (!known.insert(Lower(email)).second) continue;
        fresh.push_back(ContactForSender(name, email));
    }
    if (fresh.empty()) return 0;
    return store.SaveAll(fresh) ? static_cast<int>(fresh.size()) : 0;
}

} // namespace UltraMail
