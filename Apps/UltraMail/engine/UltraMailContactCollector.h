// Apps/UltraMail/engine/UltraMailContactCollector.h
// Auto-fills the address book from mail traffic, by these rules:
//
//  - The people the user writes to (To and Cc of what they send) are added:
//    writing to someone is the user's own act, so this holds even for an
//    address they once deleted.
//  - From incoming mail only senders in the known-sender registry are added -
//    the crowdfunding platform a project was backed on, the shop an order came
//    from, the creator-support service a membership runs through - filed as
//    business contacts under Services. That is what makes the registry a
//    source of new business contacts and not only a source of icons.
//  - Any other sender is not added. A stranger who writes - and every spammer
//    whose mail reaches the inbox - is no contact of the user's: added, it
//    showed as a suggestion when writing and lost its "New sender" badge,
//    reading as someone the user knows. It joins the address book when the
//    user writes to it or adds it.
//  - Never a sender the user blocked (SenderLists), nor an address they
//    deleted from the address book (ContactStore::Forget) - before, a deleted
//    sender was back after its next message.
//  - A contact that exists is never modified or reclassified.
//
// Version: 0.4.0 - incoming mail adds only known services, never a blocked
//                  or forgotten address; the rules above; Collect reads
//                  "Name <address>"
// Version: 0.3.0 - CollectSenders: a batch of senders (a sync's new mail) in one
//                  read of the address book and one transaction
// Version: 0.2.0 - CollectSender files a known service under Services with the
//                  service's name as the organization
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailContactStore.h"

#include <string>
#include <utility>
#include <vector>

namespace UltraMail {

class ContactCollector {
public:
    // Add (name, email) to `store` in `section` if no contact already has that
    // email - the people the user writes to, whether or not they were once
    // forgotten. Returns true if a new contact was created. A blank email is
    // ignored. Existing contacts are never modified or reclassified.
    // `recipient` is a bare address or "Name <address>", as a compose field
    // holds it; the name in it is used when `name` is empty.
    static bool Collect(ContactStore& store, const std::string& name,
                        const std::string& recipient,
                        ContactSection section = ContactSection::Other);

    // Collect one message's sender: added only when it is a known service
    // (filed under Services, carrying the service's name as the organization
    // and a note saying what kind of service it is and that UltraMail added
    // it), not blocked, not forgotten and not in the address book. Returns
    // true if a new contact was created. A sender the user has already filed
    // stays where they put it, whatever the registry says.
    static bool CollectSender(ContactStore& store, const std::string& name,
                              const std::string& email);

    // Collect the senders of a batch of messages - (name, address) pairs, a
    // sync's new mail - each as CollectSender would. An address that comes
    // more than once is collected once, under the first name it came with.
    // One read of the stored addresses and one transaction for all the new
    // contacts, however many senders: collected one by one, each was a search
    // of the whole address book and a commit of its own, and after a large
    // sync that held the window for tens of seconds on Windows. Returns the
    // number of contacts created (0 when the batch could not be saved).
    static int CollectSenders(ContactStore& store,
                              const std::vector<std::pair<std::string, std::string>>& senders);
};

} // namespace UltraMail
