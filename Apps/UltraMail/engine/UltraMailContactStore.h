// Apps/UltraMail/engine/UltraMailContactStore.h
// The UltraMail address book on UltraDatabase: contacts organised into
// sections, each with any number of emails / phones. A global store (not
// per-account) so contacts are shared across accounts, mirroring LocalStore's
// UltraDatabase-backed design.
// Version: 0.1.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailContacts.h"

#include <UltraDatabase/UltraDatabaseCore.h>

#include <string>
#include <vector>

namespace UltraMail {

class ContactStore {
public:
    // Register an UltraDatabase connection and bring the schema up to date.
    // `databasePath` is a file path (created if absent) or ":memory:".
    UltraDbResult Open(const std::string& connectionName,
                       const std::string& databasePath);

    bool IsOpen() const { return !connection_.empty(); }

    // Insert (id == 0) or update a contact together with its emails/phones,
    // atomically. On insert, `contact.id` is filled with the new id.
    UltraDbResult Save(Contact& contact);

    UltraDbResult Get(int64_t id, Contact& out) const;
    UltraDbResult Remove(int64_t id);

    // Contacts in a section, ordered by display name.
    UltraDbResult ListBySection(ContactSection section,
                                std::vector<Contact>& out) const;

    // Free-text search across name / organization / email address.
    UltraDbResult Search(const std::string& query, std::vector<Contact>& out) const;

    // Per-section counts for the sidebar (every primary section present, even
    // when empty).
    UltraDbResult GetSectionCounts(std::vector<SectionCount>& out) const;

private:
    UltraDbResult LoadChildren(Contact& c) const;
    UltraDbResult ReplaceChildren(UltraDbHandle tx, int64_t contactId,
                                  const Contact& c);

    std::string connection_;
};

} // namespace UltraMail
