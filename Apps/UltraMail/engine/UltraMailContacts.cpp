// Apps/UltraMail/engine/UltraMailContacts.cpp
// ContactSection <-> string mapping and section ordering.
// Version: 0.2.0 - EditableEmailIndex, ReplaceContactEmail
// Version: 0.1.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailContacts.h"

#include <algorithm>
#include <cctype>

namespace UltraMail {

std::string ToString(ContactSection s) {
    switch (s) {
        case ContactSection::Family:   return "family";
        case ContactSection::Friends:  return "friends";
        case ContactSection::Work:     return "work";
        case ContactSection::Leisure:  return "leisure";
        case ContactSection::Services: return "services";
        case ContactSection::Other:
        default:                       return "other";
    }
}

ContactSection ContactSectionFromString(const std::string& s) {
    if (s == "family")   return ContactSection::Family;
    if (s == "friends")  return ContactSection::Friends;
    if (s == "work")     return ContactSection::Work;
    if (s == "leisure")  return ContactSection::Leisure;
    if (s == "services") return ContactSection::Services;
    return ContactSection::Other;
}

std::string DisplayName(ContactSection s) {
    switch (s) {
        case ContactSection::Family:   return "Family";
        case ContactSection::Friends:  return "Friends";
        case ContactSection::Work:     return "Work";
        case ContactSection::Leisure:  return "Leisure";
        case ContactSection::Services: return "Services";
        case ContactSection::Other:
        default:                       return "Other";
    }
}

const std::vector<ContactSection>& PrimarySections() {
    static const std::vector<ContactSection> sections = {
        ContactSection::Family,  ContactSection::Friends, ContactSection::Work,
        ContactSection::Leisure, ContactSection::Services
    };
    return sections;
}

namespace {

bool SameAddress(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

} // namespace

std::size_t EditableEmailIndex(const std::vector<ContactEmail>& emails,
                               const std::string& address) {
    if (!address.empty())
        for (std::size_t i = 0; i < emails.size(); ++i)
            if (SameAddress(emails[i].address, address)) return i;
    for (std::size_t i = 0; i < emails.size(); ++i)
        if (emails[i].primary) return i;
    return emails.empty() ? std::string::npos : 0;
}

void ReplaceContactEmail(std::vector<ContactEmail>& emails, std::size_t index,
                         const std::string& address) {
    if (index >= emails.size()) {
        if (!address.empty()) {
            ContactEmail e; e.address = address; e.primary = emails.empty();
            emails.push_back(e);
        }
    } else {
        ContactEmail edited = emails[index];
        edited.address = address;
        std::vector<ContactEmail> out;
        std::size_t at = std::string::npos;   // where `edited` is in `out`
        for (std::size_t i = 0; i < emails.size(); ++i) {
            if (i == index) {
                if (!address.empty()) { at = out.size(); out.push_back(edited); }
            } else if (!address.empty() && SameAddress(emails[i].address, address)) {
                edited.primary = edited.primary || emails[i].primary;   // merged into it
            } else {
                out.push_back(emails[i]);
            }
        }
        if (at != std::string::npos) out[at].primary = edited.primary;
        emails = std::move(out);
    }
    if (!emails.empty() && std::none_of(emails.begin(), emails.end(),
                                        [](const ContactEmail& e) { return e.primary; }))
        emails.front().primary = true;
}

} // namespace UltraMail
