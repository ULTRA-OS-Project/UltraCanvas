// Apps/UltraMail/engine/UltraMailContactCollector.h
// Auto-fills the address book from mail traffic: every address the user
// corresponds with is remembered (into the Other section) unless a contact
// with that email already exists. This is the "collected addresses" behaviour
// the concept describes, feeding compose autocomplete later.
// Version: 0.1.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailContactStore.h"

#include <string>

namespace UltraMail {

class ContactCollector {
public:
    // Add (name, email) to `store` in `section` if no contact already has that
    // email. Returns true if a new contact was created. A blank email is
    // ignored. Existing contacts are never modified or reclassified.
    static bool Collect(ContactStore& store, const std::string& name,
                        const std::string& email,
                        ContactSection section = ContactSection::Other);
};

} // namespace UltraMail
