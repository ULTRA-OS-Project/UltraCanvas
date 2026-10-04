// Apps/UltraMail/engine/UltraMailRecipientComplete.h
// Address completion for the compose window's To, Cc and Bcc fields: a field
// holds a comma-separated list, and the part being typed - after the last
// comma - is matched against the address book. Picking a suggestion replaces
// that part with "Name <address>, " and leaves the recipients before it.
// Headless: the compose window feeds it the field text and the contacts.
// Version: 0.2.0 - ranked by how often - and how lately - each address is written to
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailContacts.h"

#include <map>
#include <string>
#include <vector>

namespace UltraMail {

struct RecipientSuggestion {
    std::string recipient;   // what goes in the field: "Anna Schmidt <anna@example.com>"
    std::string address;     // the bare address
};

// The recipient being typed: the text after the last comma, trimmed.
std::string RecipientQuery(const std::string& fieldText);

// `fieldText` with the recipient being typed replaced by `recipient`, followed
// by ", " ready for the next one.
std::string CompleteRecipient(const std::string& fieldText, const std::string& recipient);

// The contacts' addresses that match `query` (case-insensitive): a name or
// organization word, or the address, that starts with it, or - weaker - that
// contains it. Addresses already in `fieldText` are left out. Ranked by
// `writtenTo` - how much the user writes to each address, recent mail weighing
// more (keys bare, lower case; see LocalStore::WeighSentRecipients) - most
// first; among equals a match at a word's start comes before one inside it,
// then address-book order. At most `limit`; none for an empty query.
std::vector<RecipientSuggestion> SuggestRecipients(
    const std::vector<Contact>& contacts, const std::string& query,
    const std::string& fieldText, std::size_t limit = 8,
    const std::map<std::string, double>* writtenTo = nullptr);

} // namespace UltraMail
