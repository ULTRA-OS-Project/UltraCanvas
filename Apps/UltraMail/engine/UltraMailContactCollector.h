// Apps/UltraMail/engine/UltraMailContactCollector.h
// Auto-fills the address book from mail traffic: every address the user
// corresponds with is remembered unless a contact with that email already
// exists. This is the "collected addresses" behaviour the concept describes,
// feeding compose autocomplete later.
//
// A sender that belongs to the known-sender registry is filed as a business
// contact rather than as a loose address: the crowdfunding platform a project
// was backed on, the shop an order came from, the creator-support service a
// membership runs through. That is what makes the registry a source of new
// business contacts and not only a source of icons.
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
    // email. Returns true if a new contact was created. A blank email is
    // ignored. Existing contacts are never modified or reclassified.
    static bool Collect(ContactStore& store, const std::string& name,
                        const std::string& email,
                        ContactSection section = ContactSection::Other);

    // Collect one message's sender. An address in the known-sender registry is
    // filed under Services, carrying the service's name as the organization and
    // a note saying what kind of service it is and that UltraMail added it;
    // anything else lands in Other exactly as before. Returns true if a new
    // contact was created. Existing contacts are never modified or
    // reclassified — a sender the user has already filed stays where they put
    // it, whatever the registry says.
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
