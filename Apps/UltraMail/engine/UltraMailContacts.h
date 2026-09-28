// Apps/UltraMail/engine/UltraMailContacts.h
// Contact data types for the UltraMail address book. Contacts are organised
// into sections — Friends, Work, Leisure, Services (plus Other for the
// uncategorised) — and each carries any number of email addresses and phone
// numbers.
// Version: 0.1.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace UltraMail {

// The address-book sections the user sees as top-level groups.
enum class ContactSection {
    Family = 0,
    Friends,
    Work,
    Leisure,
    Services,
    Other
};

std::string    ToString(ContactSection section);
ContactSection ContactSectionFromString(const std::string& s);
std::string    DisplayName(ContactSection section);       // "Friends", "Work", …

// The four primary sections in display order (excludes Other).
const std::vector<ContactSection>& PrimarySections();

struct ContactEmail {
    std::string address;
    std::string label;        // "home" | "work" | …
    bool        primary = false;
};

struct ContactPhone {
    std::string number;
    std::string label;        // "mobile" | "home" | "work" | …
};

struct Contact {
    int64_t        id = 0;    // 0 == not yet stored
    std::string    displayName;
    std::string    organization;
    std::string    notes;
    ContactSection section = ContactSection::Other;
    // A group of the user's own ("Add group"); empty when the contact sits in
    // its section. The section still says what kind of sender it is (the badge
    // colour), whichever group it is filed under.
    std::string    group;
    std::vector<ContactEmail> emails;
    std::vector<ContactPhone> phones;

    // Convenience: the primary email (or first, or empty).
    std::string PrimaryEmail() const {
        for (const auto& e : emails) if (e.primary) return e.address;
        return emails.empty() ? std::string() : emails.front().address;
    }
};

// A group of the user's own and how many contacts are filed in it.
struct GroupCount {
    std::string name;
    int         count = 0;
};

// Count of contacts in one section, for the sidebar.
struct SectionCount {
    ContactSection section;
    int            count = 0;
};

} // namespace UltraMail
