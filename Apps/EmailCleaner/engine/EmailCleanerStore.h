// Apps/EmailCleaner/engine/EmailCleanerStore.h
// The EmailCleaner analysis database: one UltraDatabase (SQLite) connection
// holding every message the app has looked at, its attachments, and the
// keyword hits that explain its classification.
//
// This is the "load the email into a database to analyse them" half of the
// app. Nothing here fetches mail — the ingest does that — and nothing here
// draws: the aggregate queries below are exactly the shapes the map view
// (sender blocks), the timetable (weekday x hour) and the timeline chart
// consume, so the UI never runs SQL of its own.
//
// Every query goes through bound parameters; no caller-supplied string is ever
// concatenated into SQL.
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "EmailCleanerTypes.h"

#include <UltraDatabase/UltraDatabaseCore.h>
#include <UltraDatabase/UltraDatabaseValue.h>

#include <string>
#include <vector>

namespace EmailCleaner {

// An account as the analysis database knows it (mirrors the mail account; no
// secrets — those stay in the credential vault).
struct StoredAccount {
    std::string accountId;
    std::string displayName;
    std::string email;
    std::string shortName;
};

// Where an account's messages come from, and how far the ingest has got.
struct IngestState {
    std::string accountId;
    std::string folder;
    int64_t     lastUid   = 0;
    int64_t     lastRun   = 0;   // epoch seconds
    int         messages  = 0;   // messages ingested for this folder so far
};

// One blocklist entry: a sender address, or a whole sending domain.
struct BlockEntry {
    std::string pattern;          // lowercased address or domain
    bool        isDomain = false;
    std::string reason;           // why it was blocked, for the list view
    int64_t     added = 0;        // epoch seconds

    bool Valid() const { return !pattern.empty(); }
};

class AnalysisStore {
public:
    // Register the connection and bring the schema up to date. `databasePath`
    // is a file path (created if absent) or ":memory:". Calling it again with
    // the same name and path is a no-op re-migration and keeps the existing
    // connection — re-registering would drop it, which for ":memory:" would
    // discard the database.
    UltraDbResult Open(const std::string& connectionName,
                       const std::string& databasePath);

    bool               IsOpen()     const { return !connection_.empty(); }
    const std::string& Connection() const { return connection_; }

    // ---- Accounts ----------------------------------------------------------
    UltraDbResult UpsertAccount(const StoredAccount& account);
    UltraDbResult ListAccounts(std::vector<StoredAccount>& out) const;
    // Removes the account and everything analysed for it.
    UltraDbResult RemoveAccount(const std::string& accountId);

    // ---- Messages ----------------------------------------------------------
    // Insert or replace one analysed message together with its attachments and
    // keyword hits. Runs in a transaction, so a message is never half-stored.
    UltraDbResult UpsertMessage(const AnalyzedMessage& message);

    // Bulk variant: one transaction for the whole batch, which is what makes
    // a first full-mailbox load fast.
    UltraDbResult UpsertMessages(const std::vector<AnalyzedMessage>& messages);

    // True when this account/folder/uid has already been analysed — the ingest
    // uses it to skip work on a re-scan.
    bool HasMessage(const std::string& accountId, const std::string& folder,
                    int64_t uid) const;

    // Messages matching the filter, most recent first. Attachments and hits
    // are not loaded (use GetAttachments / GetHits for the detail view).
    UltraDbResult ListMessages(const MessageFilter& filter,
                               std::vector<AnalyzedMessage>& out) const;

    UltraDbResult GetAttachments(const std::string& accountId, const std::string& folder,
                                 int64_t uid, std::vector<AttachmentRecord>& out) const;
    UltraDbResult GetHits(const std::string& accountId, const std::string& folder,
                          int64_t uid, std::vector<KeywordHit>& out) const;

    // Re-classify support: drop everything analysed for an account (or for
    // everything, when accountId is empty) so the ingest can run again.
    UltraDbResult ClearMessages(const std::string& accountId);

    // ---- Aggregates: the map view -----------------------------------------
    // One block per sender address matching the filter, largest first by the
    // given metric. `limit` 0 means "every sender".
    UltraDbResult ListSenderBlocks(const MessageFilter& filter,
                                   SenderMetric metric,
                                   int limit,
                                   std::vector<SenderBlock>& out) const;

    // The same rollup one level up, keyed by sending domain.
    UltraDbResult ListDomainBlocks(const MessageFilter& filter,
                                   SenderMetric metric,
                                   int limit,
                                   std::vector<SenderBlock>& out) const;

    // ---- Aggregates: the timetable and the timeline ------------------------
    // Weekday x hour grid (UTC) of the messages matching the filter.
    UltraDbResult GetTimetable(const MessageFilter& filter, Timetable& out) const;

    // Message counts per time bucket, oldest first. Empty buckets between the
    // first and last hit are filled in, so the chart shows real gaps.
    UltraDbResult GetTimeline(const MessageFilter& filter, TimeBucket bucket,
                              std::vector<TimelinePoint>& out) const;

    // ---- Aggregates: rollups ----------------------------------------------
    UltraDbResult GetCategoryTotals(const MessageFilter& filter,
                                    std::vector<CategoryTotal>& out) const;
    UltraDbResult GetAttachmentTypeTotals(const MessageFilter& filter,
                                          std::vector<AttachmentTypeTotal>& out) const;
    UltraDbResult GetOverview(const MessageFilter& filter, StoreOverview& out) const;

    // The terms that fired most often across the filtered messages — the
    // evidence summary behind a sender or a category.
    UltraDbResult GetTopKeywords(const MessageFilter& filter, int limit,
                                 std::vector<KeywordHit>& out) const;

    // ---- Blocklist ---------------------------------------------------------
    // Senders the user has decided not to hear from. Purely local: it changes
    // what the ingest marks and what the map shows, and never touches the
    // mail server.
    UltraDbResult AddBlock(const BlockEntry& entry);
    UltraDbResult RemoveBlock(const std::string& pattern);
    UltraDbResult ListBlocks(std::vector<BlockEntry>& out) const;
    // True when the address, or the domain it belongs to, is blocked.
    bool IsBlocked(const std::string& senderAddr, const std::string& domain) const;

    // Re-stamp the `blocked` flag on stored messages after the list changed,
    // so the map reflects a block without a re-ingest. Returns rows touched in
    // the result's affectedRows.
    UltraDbResult ApplyBlocklistToMessages();

    // ---- Unsubscribe -------------------------------------------------------
    // The newest unsubscribe offer seen from a sender (or, for a domain
    // target, from anyone under it). `found` is false when nobody offered one.
    UltraDbResult GetUnsubscribeOffer(const MessageFilter& filter,
                                      std::string& outMailto,
                                      std::string& outMailtoSubject,
                                      std::string& outUrl,
                                      bool& outOneClick,
                                      bool& found) const;

    // ---- Ingest bookkeeping ------------------------------------------------
    UltraDbResult UpsertIngestState(const IngestState& state);
    UltraDbResult ListIngestState(const std::string& accountId,
                                  std::vector<IngestState>& out) const;
    UltraDbResult GetLastUid(const std::string& accountId, const std::string& folder,
                             int64_t& out) const;

private:
    // Build the shared WHERE clause for a filter, appending its bound values
    // to `params`. `alias` is the messages-table alias in the query ("m").
    static std::string BuildWhere(const MessageFilter& filter,
                                  const std::string& alias,
                                  UltraDbParams& params);

    std::string connection_;
};

} // namespace EmailCleaner
