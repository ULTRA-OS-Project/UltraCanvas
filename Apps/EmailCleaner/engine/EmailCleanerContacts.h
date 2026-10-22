// Apps/EmailCleaner/engine/EmailCleanerContacts.h
// The contacts review: UltraMail's address book held against the mail the
// owner actually exchanged with each contact.
//
// UltraMail fills its address book by itself - every sender it syncs is
// collected unless a contact already has the address - so after a few years
// it holds every shop, newsletter and stranger that ever wrote. The one fact
// that tells a real contact from a collected one is whether the owner ever
// wrote back. So every contact gets the number of messages the owner sent to
// it (the Sent folder's To and Cc lines) and the number it sent the owner,
// and a verdict:
//
//   Keep    the owner wrote to it at least once
//   Delete  never written to, and only collected (section Other or Services,
//           no group of the user's own) - selected for deletion
//   Review  never written to, but filed by the user (Family, Friends, Work,
//           Leisure or an own group), or without an email address at all -
//           not selected, because the user put it there on purpose
//
// The list is ordered by messages sent, then received, then name. Everything
// here is pure but RemoveContacts, which deletes from the address book.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "EmailCleanerTypes.h"

#include "UltraMailContactStore.h"
#include "UltraMailContacts.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace EmailCleaner {

enum class ContactVerdict {
    Keep = 0,   // written to
    Delete,     // never written to, only collected: selected for deletion
    Review      // never written to, but filed by the user / no address
};

// "Keep" | "Delete" | "Review" - the tag on the row.
std::string TagLabel(ContactVerdict verdict);

// One row of the contacts review.
struct ContactReview {
    int64_t                  contactId = 0;
    std::string              displayName;
    std::string              organization;
    std::string              place;        // the section or group it is filed in
    std::vector<std::string> addresses;    // lowercased, each once

    int     sent         = 0;   // messages the owner sent to any of its addresses
    int     received     = 0;   // messages any of its addresses sent the owner
    int64_t lastSent     = 0;   // epoch seconds, 0 = never
    int64_t lastReceived = 0;

    ContactVerdict verdict = ContactVerdict::Review;

    // The name to show: the display name, else the first address.
    std::string Name() const;
    // Why it got its verdict, in a few words ("you wrote 12 times", ...).
    std::string Reason() const;
};

// Review every contact against the correspondence counts
// (AnalysisStore::GetCorrespondence). Ordered by sent, then received, then
// name, case-insensitively.
std::vector<ContactReview> ReviewContacts(
    const std::vector<UltraMail::Contact>& contacts,
    const std::map<std::string, Correspondence>& correspondence);

struct ContactReviewSummary {
    int total    = 0;
    int keep     = 0;
    int toDelete = 0;   // verdict Delete
    int review   = 0;
};

ContactReviewSummary Summarize(const std::vector<ContactReview>& reviews);

// "312 contacts: you wrote to 48 (keep), 251 only collected and never written
// to (marked for deletion), 13 to review."
std::string DescribeReview(const ContactReviewSummary& summary);

// Delete the contacts `ids` from the address book in one transaction
// (ContactStore::RemoveAll): all of them, or none. `removed` is how many were
// deleted - every id on success, 0 on failure.
UltraDbResult RemoveContacts(UltraMail::ContactStore& store,
                             const std::vector<int64_t>& ids, int& removed);

} // namespace EmailCleaner
