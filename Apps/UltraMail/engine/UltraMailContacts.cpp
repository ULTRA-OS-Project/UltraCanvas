// Apps/UltraMail/engine/UltraMailContacts.cpp
// ContactSection <-> string mapping and section ordering.
// Version: 0.1.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailContacts.h"

namespace UltraMail {

std::string ToString(ContactSection s) {
    switch (s) {
        case ContactSection::Friends:  return "friends";
        case ContactSection::Work:     return "work";
        case ContactSection::Leisure:  return "leisure";
        case ContactSection::Services: return "services";
        case ContactSection::Other:
        default:                       return "other";
    }
}

ContactSection ContactSectionFromString(const std::string& s) {
    if (s == "friends")  return ContactSection::Friends;
    if (s == "work")     return ContactSection::Work;
    if (s == "leisure")  return ContactSection::Leisure;
    if (s == "services") return ContactSection::Services;
    return ContactSection::Other;
}

std::string DisplayName(ContactSection s) {
    switch (s) {
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
        ContactSection::Friends, ContactSection::Work,
        ContactSection::Leisure, ContactSection::Services
    };
    return sections;
}

} // namespace UltraMail
