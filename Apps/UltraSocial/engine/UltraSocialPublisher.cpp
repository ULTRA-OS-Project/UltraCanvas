// Apps/UltraSocial/engine/UltraSocialPublisher.cpp
// Version: 0.1.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraSocialPublisher.h"

#include "UltraSocialComposer.h"
#include "UltraSocialConnector.h"

namespace UltraSocial {

HistoryEntry PublishOne(Store& store,
                        CredentialVault& vault,
                        const Account& account,
                        const PostDraft& draft) {
    HistoryEntry entry;
    entry.accountId  = account.accountId;
    entry.network    = account.network;
    entry.mediaCount = static_cast<int>(draft.media.size());

    auto connector = CreateConnector(account.network);
    if (!connector) {
        entry.text  = draft.text;
        entry.error = "no connector for this network";
        store.AddHistory(entry);
        return entry;
    }

    std::vector<std::string> warnings;
    AdaptedPost post = AdaptDraft(draft, account.network,
                                  connector->Capabilities(), warnings);
    entry.text = post.text;

    auto problems = connector->ValidateDraft(post);
    std::string credentials;
    if (!problems.empty()) {
        entry.error = problems.front();
    } else if (!vault.Retrieve(account.accountId, credentials)) {
        entry.error = "no stored credentials — remove and re-add the account";
    } else {
        PostResult posted;
        auto result = connector->PublishPost(account, credentials, post, posted);
        if (result) {
            entry.succeeded = true;
            entry.postId    = posted.postId;
            entry.url       = posted.url;
            // Connectors may rotate tokens during publish.
            vault.Store(account.accountId, credentials);
        } else {
            entry.error = result.message;
        }
    }

    store.AddHistory(entry);
    return entry;
}

FlushStats FlushDueOutbox(Store& store, CredentialVault& vault, int64_t now) {
    FlushStats stats;

    std::vector<OutboxEntry> due;
    if (!store.DueOutbox(now, due)) return stats;
    if (due.empty()) return stats;

    std::vector<Account> accounts;
    store.ListAccounts(accounts);

    for (const auto& queued : due) {
        const Account* account = nullptr;
        for (const auto& a : accounts) {
            if (a.accountId == queued.accountId) { account = &a; break; }
        }
        if (!account) {
            HistoryEntry gone;
            gone.accountId  = queued.accountId;
            gone.network    = queued.network;
            gone.text       = queued.draft.text;
            gone.mediaCount = static_cast<int>(queued.draft.media.size());
            gone.error      = "account was removed before the scheduled post";
            store.AddHistory(gone);
            store.RemoveOutbox(queued.id);
            ++stats.abandoned;
            continue;
        }

        // A successful attempt writes its own history row; a failed final
        // attempt does too. Intermediate failures only annotate the queue
        // entry, so the history stays one row per delivered outcome.
        auto connector = CreateConnector(account->network);
        std::vector<std::string> warnings;
        AdaptedPost post = AdaptDraft(queued.draft, account->network,
                                      connector->Capabilities(), warnings);

        std::string error;
        std::string credentials;
        PostResult posted;
        auto problems = connector->ValidateDraft(post);
        if (!problems.empty()) {
            error = problems.front();
        } else if (!vault.Retrieve(account->accountId, credentials)) {
            error = "no stored credentials — remove and re-add the account";
        } else {
            auto result = connector->PublishPost(*account, credentials, post,
                                                 posted);
            if (result) {
                vault.Store(account->accountId, credentials);
            } else {
                error = result.message;
            }
        }

        HistoryEntry entry;
        entry.accountId  = account->accountId;
        entry.network    = account->network;
        entry.text       = post.text;
        entry.mediaCount = static_cast<int>(post.media.size());

        if (error.empty()) {
            entry.succeeded = true;
            entry.postId    = posted.postId;
            entry.url       = posted.url;
            store.AddHistory(entry);
            store.RemoveOutbox(queued.id);
            ++stats.published;
        } else if (queued.attempts + 1 >= kOutboxMaxAttempts) {
            entry.error = error + " (gave up after " +
                          std::to_string(queued.attempts + 1) + " attempts)";
            store.AddHistory(entry);
            store.RemoveOutbox(queued.id);
            ++stats.abandoned;
        } else {
            store.RescheduleOutbox(
                queued.id,
                now + kOutboxRetryBackoffSeconds * (queued.attempts + 1),
                queued.attempts + 1, error);
            ++stats.rescheduled;
        }
    }
    return stats;
}

} // namespace UltraSocial
