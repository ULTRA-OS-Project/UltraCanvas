// Apps/UltraMail/engine/UltraMailContactCollector.cpp
// Version: 0.1.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailContactCollector.h"

#include <algorithm>
#include <cctype>
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

bool ContactCollector::Collect(ContactStore& store, const std::string& name,
                               const std::string& email, ContactSection section) {
    if (email.empty()) return false;
    const std::string target = Lower(email);

    // Already known?
    std::vector<Contact> hits;
    if (store.Search(email, hits)) {
        for (const auto& c : hits)
            for (const auto& e : c.emails)
                if (Lower(e.address) == target) return false;
    }

    Contact c;
    c.displayName = name.empty() ? email : name;
    c.section = section;
    ContactEmail e; e.address = email; e.primary = true;
    c.emails.push_back(e);
    return store.Save(c).success;
}

} // namespace UltraMail
