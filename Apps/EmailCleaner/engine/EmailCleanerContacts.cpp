// Apps/EmailCleaner/engine/EmailCleanerContacts.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "EmailCleanerContacts.h"

#include <algorithm>
#include <cctype>

namespace EmailCleaner {

namespace {

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string Plural(int n, const std::string& one, const std::string& many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

// Filed by the user rather than by UltraMail's collector, which only ever
// uses Other and Services.
bool FiledByUser(const UltraMail::Contact& contact) {
    if (!contact.group.empty()) return true;
    return contact.section != UltraMail::ContactSection::Other &&
           contact.section != UltraMail::ContactSection::Services;
}

} // namespace

std::string TagLabel(ContactVerdict verdict) {
    switch (verdict) {
        case ContactVerdict::Keep:   return "Keep";
        case ContactVerdict::Delete: return "Delete";
        case ContactVerdict::Review: return "Review";
    }
    return "Review";
}

std::string ContactReview::Name() const {
    if (!displayName.empty()) return displayName;
    return addresses.empty() ? std::string("(no name)") : addresses.front();
}

std::string ContactReview::Reason() const {
    switch (verdict) {
        case ContactVerdict::Keep:
            return "you wrote " + Plural(sent, "time", "times");
        case ContactVerdict::Delete:
            return "never written to - collected from incoming mail";
        case ContactVerdict::Review:
            if (addresses.empty()) return "no email address";
            return "never written to, but filed by you in " + place;
    }
    return "";
}

std::vector<ContactReview> ReviewContacts(
    const std::vector<UltraMail::Contact>& contacts,
    const std::map<std::string, Correspondence>& correspondence) {
    std::vector<ContactReview> out;
    out.reserve(contacts.size());

    for (const UltraMail::Contact& contact : contacts) {
        ContactReview review;
        review.contactId    = contact.id;
        review.displayName  = contact.displayName;
        review.organization = contact.organization;
        review.place        = contact.group.empty()
            ? UltraMail::DisplayName(contact.section) : contact.group;

        for (const UltraMail::ContactEmail& email : contact.emails) {
            const std::string address = Lower(email.address);
            if (address.empty() ||
                std::find(review.addresses.begin(), review.addresses.end(), address) !=
                    review.addresses.end())
                continue;
            review.addresses.push_back(address);

            auto it = correspondence.find(address);
            if (it == correspondence.end()) continue;
            const Correspondence& c = it->second;
            review.sent         += c.sent;
            review.received     += c.received;
            review.lastSent      = std::max(review.lastSent, c.lastSent);
            review.lastReceived  = std::max(review.lastReceived, c.lastReceived);
        }

        if (review.sent > 0)
            review.verdict = ContactVerdict::Keep;
        else if (review.addresses.empty() || FiledByUser(contact))
            review.verdict = ContactVerdict::Review;
        else
            review.verdict = ContactVerdict::Delete;
        out.push_back(std::move(review));
    }

    std::stable_sort(out.begin(), out.end(),
                     [](const ContactReview& a, const ContactReview& b) {
        if (a.sent != b.sent) return a.sent > b.sent;
        if (a.received != b.received) return a.received > b.received;
        return Lower(a.Name()) < Lower(b.Name());
    });
    return out;
}

ContactReviewSummary Summarize(const std::vector<ContactReview>& reviews) {
    ContactReviewSummary summary;
    summary.total = static_cast<int>(reviews.size());
    for (const ContactReview& review : reviews) {
        switch (review.verdict) {
            case ContactVerdict::Keep:   ++summary.keep;     break;
            case ContactVerdict::Delete: ++summary.toDelete; break;
            case ContactVerdict::Review: ++summary.review;   break;
        }
    }
    return summary;
}

std::string DescribeReview(const ContactReviewSummary& summary) {
    if (summary.total == 0) return "The address book is empty.";
    std::string text = Plural(summary.total, "contact", "contacts") +
                       ": you wrote to " + std::to_string(summary.keep) + " (keep), " +
                       std::to_string(summary.toDelete) +
                       " only collected and never written to (marked for deletion)";
    if (summary.review > 0) text += ", " + std::to_string(summary.review) + " to review";
    return text + ".";
}

UltraDbResult RemoveContacts(UltraMail::ContactStore& store,
                             const std::vector<ContactReview>& contacts, int& removed) {
    removed = 0;
    std::vector<int64_t>     ids;
    std::vector<std::string> addresses;
    for (const ContactReview& contact : contacts) {
        ids.push_back(contact.contactId);
        addresses.insert(addresses.end(), contact.addresses.begin(), contact.addresses.end());
    }
    UltraDbResult r = store.RemoveAll(ids);
    if (!r) return r;
    removed = static_cast<int>(ids.size());
    // Without this UltraMail would collect the next message's sender again.
    return store.Forget(addresses);
}

} // namespace EmailCleaner
