// Apps/UltraMail/engine/UltraMailOutbox.h
// The persistent send queue. Drafts are queued to an UltraDatabase-backed store
// (surviving restarts); Flush attempts to send each pending item via the SMTP
// plug-in, removing successes and recording failures for retry. Keeps sending
// off the compose path so Send never blocks and survives being offline.
// Version: 0.2.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailComposer.h"
#include "UltraMailSender.h"

#include <UltraDatabase/UltraDatabaseCore.h>
#include <UltraNet/UltraNetPlugins.h>

#include <functional>
#include <string>
#include <vector>

namespace UltraMail {

struct OutboxItem {
    int64_t     id = 0;
    std::string accountId;
    std::string serverUrl;
    Draft       draft;
    int         attempts = 0;
    std::string lastError;
};

// Persistent queue storage (contacts-style, on its own UltraDatabase connection).
class OutboxStore {
public:
    UltraDbResult Open(const std::string& connectionName, const std::string& databasePath);

    // Mirrors LocalStore / ContactStore, so the UI can tell "the queue is
    // unavailable" from "the queue is empty" and say so.
    bool IsOpen() const { return !connection_.empty(); }

    UltraDbResult Enqueue(const std::string& accountId, const std::string& serverUrl,
                          const Draft& draft, int64_t& outId);
    UltraDbResult ListPending(std::vector<OutboxItem>& out) const;
    UltraDbResult Remove(int64_t id);
    UltraDbResult MarkFailed(int64_t id, const std::string& error);
    UltraDbResult PendingCount(int& out) const;

private:
    UltraDbResult LoadAttachments(OutboxItem& item) const;
    std::string connection_;
};

// The queue operator: enqueue + flush.
class Outbox {
public:
    explicit Outbox(OutboxStore& store) : store_(store) {}

    UltraDbResult Queue(const std::string& accountId, const std::string& serverUrl,
                        const Draft& draft, int64_t& outId) {
        return store_.Enqueue(accountId, serverUrl, draft, outId);
    }

    // `lastFailure` is the result of the most recent failed send, so the UI can
    // tell the user *why* a message stayed in the queue instead of only that it
    // did. Meaningless when `failed` is 0.
    struct FlushStats {
        int             sent = 0;
        int             failed = 0;
        UltraNetResult  lastFailure;
    };

    // Attempt to send every pending item through `smtp`. `credentialFor` maps an
    // account id to its password / token (resolved from the credential vault).
    FlushStats Flush(IMailProtocolPlugin& smtp,
                     const std::function<std::string(const std::string&)>& credentialFor);

private:
    OutboxStore& store_;
};

} // namespace UltraMail
