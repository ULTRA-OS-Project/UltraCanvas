// Apps/UltraMail/engine/UltraMailFeedPublisher.h
// Publishes newly fetched mail to the desktop feed over UltraMessage
// (Masterfile_modules.md §13, topic `mail.message`), so the message centre
// lists it next to chat and notifications. Headless: the sync worker calls
// Publish() for every envelope it stores; the publisher decides what
// qualifies (unread, recent) and rate-limits an initial sync so a mailbox's
// history never floods the feed. It also puts UltraMail's "new mail"
// notification on screen (Notify): a `system.notification` the UltraMessage
// broker hands to the desktop's notification server, with the click on it
// reported back. Without the UltraMessage module in the build
// (ULTRAMAIL_HAVE_ULTRAMESSAGE unset) it compiles to a no-op.
// Version: 0.2.0 - Notify: the new-mail notification on screen, and its click
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailTypes.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>

namespace UltraMail {

struct FeedPolicy {
    int maxAgeDays = 7;              // older mail is history, not news
    int maxPerAccountPerWindow = 100; // an initial sync publishes at most this many...
    int windowSeconds = 600;         // ...per account and window
    bool skipAutomated = false;      // bulk / list mail: still news for now
};

// The notification UltraMail puts on screen when new mail arrives.
struct MailNotification {
    std::string accountId;
    std::string summary;    // "New mail from Ada Lovelace"
    std::string body;       // the subject, or a line per message
    std::string iconPath;   // UltraMail's icon, for the servers that show one
};

class FeedPublisher {
public:
    FeedPublisher();
    ~FeedPublisher();
    FeedPublisher(const FeedPublisher&) = delete;
    FeedPublisher& operator=(const FeedPublisher&) = delete;

    // True when this build links UltraMessage.
    static bool Compiled();

    // A private bus instead of the user's (tests). Takes effect on the next
    // connect; empty paths are the platform defaults.
    void SetBusPath(const std::string& busPath, const std::string& journalPath = "");

    // Off: Publish() decides as usual but posts nothing. Default on.
    void SetEnabled(bool enabled);
    bool Enabled() const;
    void SetPolicy(const FeedPolicy& policy);

    // The address and name the feed shows for an account id; called whenever
    // the account list is (re)loaded. Unknown ids fall back to the id.
    void SetAccount(const std::string& accountId, const std::string& email, const std::string& displayName);

    // Pure: does this envelope belong in the feed under `policy`?
    static bool Qualifies(const MessageEnvelope& m, int64_t nowEpochSeconds, const FeedPolicy& policy = {});

    // Admits at most policy.maxPerAccountPerWindow messages per account and
    // window; the first call in a new window opens it. Thread-safe.
    bool Admit(const std::string& accountId, int64_t nowEpochSeconds);

    // Called on the sync worker for every stored envelope. Connects to the
    // bus on first use (hosting a broker when none runs, like every
    // UltraMessage endpoint). Returns true when the message was posted.
    bool Publish(const MessageEnvelope& m);

    // Puts a notification on screen through UltraMessage: a
    // `system.notification` from UltraMail (category email.arrived, its one
    // action "default" = Open) that the broker's presenter hands to the
    // desktop's notification server. NoJournal: the feed already lists every
    // message as a mail.message, and the notification is only the alert.
    // Not subject to SetEnabled. Returns the notification's bus id, empty
    // when it could not be posted (LastError says why).
    std::string Notify(const MailNotification& notification);

    // Called on the bus thread with (notification id, action id) when the
    // user clicks one of the notifications Notify posted. Keep it short:
    // hand the work to the UI thread.
    using NotificationActionFn = std::function<void(const std::string&, const std::string&)>;
    void SetOnNotificationAction(NotificationActionFn handler);

    // Drops the bus connection; the next Publish reconnects.
    void Disconnect();

    int64_t PublishedCount() const;
    std::string LastError() const;

private:
    struct AccountLabel { std::string email; std::string displayName; };
    struct Window { int64_t start = 0; int count = 0; };

    bool EnsureConnectedLocked();
    void HandleNotificationAction(const std::string& notificationId, const std::string& actionId);

    mutable std::mutex mutex_;
    bool enabled_ = true;
    FeedPolicy policy_;
    std::map<std::string, AccountLabel> accounts_;
    std::map<std::string, Window> windows_;
    std::string busPath_;
    std::string journalPath_;
    int64_t published_ = 0;
    std::string lastError_;
    uint64_t endpoint_ = 0;      // UltraMsgHandle, kept opaque here

    // Separate from mutex_: the click arrives on the bus thread, which a
    // thread holding mutex_ may be waiting on.
    std::mutex actionMutex_;
    NotificationActionFn onAction_;
    std::deque<std::string> notified_;   // the ids Notify posted, newest last
};

} // namespace UltraMail
