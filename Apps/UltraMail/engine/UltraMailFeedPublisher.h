// Apps/UltraMail/engine/UltraMailFeedPublisher.h
// Publishes newly fetched mail to the desktop feed over UltraMessage
// (Masterfile_modules.md §13, topic `mail.message`), so the message centre
// lists it next to chat and notifications. Headless: the sync worker calls
// Publish() for every envelope it stores; the publisher decides what
// qualifies (unread, recent) and rate-limits an initial sync so a mailbox's
// history never floods the feed. Without the UltraMessage module in the
// build (ULTRAMAIL_HAVE_ULTRAMESSAGE unset) it compiles to a no-op.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailTypes.h"

#include <cstdint>
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

class FeedPublisher {
public:
    FeedPublisher();
    ~FeedPublisher();
    FeedPublisher(const FeedPublisher&) = delete;
    FeedPublisher& operator=(const FeedPublisher&) = delete;

    // True when this build links UltraMessage.
    static bool Compiled();

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

    // Drops the bus connection; the next Publish reconnects.
    void Disconnect();

    int64_t PublishedCount() const;
    std::string LastError() const;

private:
    struct AccountLabel { std::string email; std::string displayName; };
    struct Window { int64_t start = 0; int count = 0; };

    bool EnsureConnectedLocked();

    mutable std::mutex mutex_;
    bool enabled_ = true;
    FeedPolicy policy_;
    std::map<std::string, AccountLabel> accounts_;
    std::map<std::string, Window> windows_;
    int64_t published_ = 0;
    std::string lastError_;
    uint64_t endpoint_ = 0;      // UltraMsgHandle, kept opaque here
};

} // namespace UltraMail
