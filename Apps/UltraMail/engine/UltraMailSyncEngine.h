// Apps/UltraMail/engine/UltraMailSyncEngine.h
// The headless sync engine: drives an IMAP-class mailbox (UltraNet's
// IMailboxProtocolPlugin) into the LocalStore. It lists folders, fetches new
// envelopes incrementally, optionally caches raw message bodies as .eml files
// (for the reading view + attachments via MimeCodec), and mirrors flag changes
// to the server.
//
// It is driven synchronously on the caller's thread; the app runs it on a
// per-account worker and marshals results to the UI. Because it depends only on
// the IMailboxProtocolPlugin interface, it is fully testable with a fake
// mailbox — no live server required.
// Version: 0.1.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailTypes.h"
#include "UltraMailLocalStore.h"

#include <UltraNet/UltraNetPlugins.h>

#include <cstdint>
#include <string>

namespace UltraMail {

struct SyncStats {
    int folders  = 0;   // folders upserted
    int messages = 0;   // envelopes upserted
    int bodies   = 0;   // full bodies fetched + cached
};

struct SyncOutcome {
    bool        ok = true;
    std::string message;
    SyncStats   stats;

    explicit operator bool() const { return ok; }
    static SyncOutcome Fail(const std::string& m) { return SyncOutcome{false, m, {}}; }
};

class SyncEngine {
public:
    // `emlDir` is the root under which raw message bodies are cached
    // (emlDir/<accountId>/<folder>/<uid>.eml).
    SyncEngine(LocalStore& store, IMailboxProtocolPlugin& mailbox, std::string emlDir)
        : store_(store), mailbox_(mailbox), emlDir_(std::move(emlDir)) {}

    // LIST folders on the server and upsert them for the account (with role
    // detection carried through from the plug-in).
    SyncOutcome SyncFolders(const std::string& accountId,
                            const std::string& serverUrl,
                            const UltraNetMailOptions& options);

    // Fetch envelopes with UID greater than the highest already stored, upsert
    // them, and — when fetchBodies is true — cache each new message's raw body.
    SyncOutcome SyncMessages(const std::string& accountId,
                             const std::string& folder,
                             const std::string& serverUrl,
                             const UltraNetMailOptions& options,
                             bool fetchBodies = false);

    // Fetch and cache one message body; returns the .eml path (empty on failure).
    std::string FetchBody(const std::string& accountId, const std::string& folder,
                          int64_t uid, const std::string& serverUrl,
                          const UltraNetMailOptions& options);

    // Path where a message body is (or would be) cached.
    std::string BodyPath(const std::string& accountId, const std::string& folder,
                         int64_t uid) const;

    // Set/clear a flag on the server (UID STORE) and in the local index.
    SyncOutcome SetFlag(const std::string& accountId, const std::string& folder,
                        int64_t uid, uint32_t ultramailFlag, bool set,
                        const std::string& serverUrl,
                        const UltraNetMailOptions& options);

private:
    LocalStore&             store_;
    IMailboxProtocolPlugin& mailbox_;
    std::string             emlDir_;
};

} // namespace UltraMail
