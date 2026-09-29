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
#include <UltraDatabase/UltraDatabaseValue.h>

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

    // Contacts filed in a section (not in a group of the user's own), ordered
    // by display name.
    UltraDbResult ListBySection(ContactSection section,
                                std::vector<Contact>& out) const;

    // Contacts filed in the user's group `name`, ordered by display name.
    UltraDbResult ListByGroup(const std::string& name, std::vector<Contact>& out) const;

    // Every contact, sections and groups alike (the sender-badge index).
    UltraDbResult ListAll(std::vector<Contact>& out) const;

    // The user's own groups, by name, with their contact counts.
    UltraDbResult ListGroups(std::vector<GroupCount>& out) const;
    // Add an empty group. Refused when the name is empty or already taken by a
    // group or a section ("Work").
    UltraDbResult AddGroup(const std::string& name);
    // Delete a group; its contacts go back to their sections.
    UltraDbResult RemoveGroup(const std::string& name);

    // File a contact in a section (leaving any group) or in a group.
    UltraDbResult MoveToSection(int64_t id, ContactSection section);
    UltraDbResult MoveToGroup(int64_t id, const std::string& group);

    // Free-text search across name / organization / email address.
    UltraDbResult Search(const std::string& query, std::vector<Contact>& out) const;

    // The contact holding exactly `address` (case-insensitive), in any of its
    // emails. `found` is false - and the result still Ok - when no contact has
    // it; a failure is a database error.
    UltraDbResult FindByEmail(const std::string& address, Contact& out, bool& found) const;

    // Per-section counts for the sidebar (every primary section present, even
    // when empty).
    UltraDbResult GetSectionCounts(std::vector<SectionCount>& out) const;

private:
    UltraDbResult LoadChildren(Contact& c) const;
    // Repair on open: names stored as raw ISO-2022-JP or 8-bit bytes.
    void RepairUndecodedNames();
    // SELECT the contacts matching `where` (a clause over contacts, no alias)
    // and their emails/phones in three queries rather than two per contact.
    UltraDbResult ListWhere(const std::string& where, const UltraDbParams& params,
                            std::vector<Contact>& out) const;
    UltraDbResult ReplaceChildren(UltraDbHandle tx, int64_t contactId,
                                  const Contact& c);

    std::string connection_;
};

} // namespace UltraMail
