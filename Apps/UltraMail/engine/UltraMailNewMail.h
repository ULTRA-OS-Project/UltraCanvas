// Apps/UltraMail/engine/UltraMailNewMail.h
// "New mail" on the screen: which of the messages a sync stored are news
// worth a notification, gathered per account until the sync ends, and the
// words of that notification. UltraMail posts it through UltraMessage
// (FeedPublisher::Notify), and the desktop's notification server draws it.
// Headless, so the rules are tested without a server or a screen.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailTypes.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace UltraMail {

// How many of the new messages a notification names; the rest are counted.
constexpr std::size_t kNewMailNamed = 3;

// What one sync (or several overlapping ones) brought into an account's inbox.
struct NewMailSummary {
    std::string accountId;
    int count = 0;                         // new unread messages
    std::vector<MessageEnvelope> latest;   // the newest of them, newest first, at most kNewMailNamed
};

// Thread-safe: Begin and Finish run on the UI thread, Note on the sync workers.
class NewMailTracker {
public:
    // Before a sync of the account's inbox starts. `inboxMaxUid` is the
    // highest UID the inbox already holds: mail that arrives above it is new.
    // 0 - the inbox is empty, the account's first download - announces
    // nothing, because that is the mailbox's history, not news. Syncs of one
    // account may overlap (a folder opened while the timer syncs it); the
    // watermark is the first one's, and the summary waits for the last.
    void Begin(const std::string& accountId, int64_t inboxMaxUid);

    // Every envelope a sync stored; what is not news is ignored.
    void Note(const MessageEnvelope& m);

    // A sync of the account ended. With `announce` false (the inbox's cache
    // was dropped and fetched again) what was noted is dropped. Returns the
    // summary when the last overlapping sync ends - count 0 when there is
    // nothing to tell - and an empty one while others still run.
    NewMailSummary Finish(const std::string& accountId, bool announce = true);

    // Pure: is `m` news, against the inbox's highest UID before the sync?
    // An unread, undeleted, non-draft message in the inbox above the mark.
    static bool IsNew(const MessageEnvelope& m, int64_t inboxMaxUid);

private:
    struct Pending {
        int active = 0;
        int64_t watermark = 0;
        bool announce = true;
        std::set<int64_t> uids;            // noted once, however many syncs store it
        NewMailSummary summary;
    };

    std::mutex mutex_;
    std::map<std::string, Pending> pending_;
};

// The notification's two lines.
struct NewMailText {
    std::string summary;   // "New mail from Ada Lovelace" / "3 new messages"
    std::string body;      // the subject / one "Sender: Subject" line per named message
};

// `accountLabel` names the account (its address) when the user has several,
// so the notification says which one; empty leaves it out. Sender and
// subject are used as stored: decode them before (DisplayHeader).
NewMailText FormatNewMail(const NewMailSummary& summary, const std::string& accountLabel = "");

} // namespace UltraMail
