// Tests/UltraMail/test_outbox.cpp
// The persistent outbox: queueing (with attachments), pending counts, and
// flushing through a fake SMTP plug-in — successes are removed, failures are
// retained with an incremented attempt count. Plus SyncService.SyncNow driving
// a fake mailbox end to end.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "UltraMailOutbox.h"
#include "UltraMailSyncService.h"
#include "UltraMailLocalStore.h"

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetPlugins.h>

#include <map>
#include <string>
#include <vector>

using namespace UltraMail;

namespace {

// SMTP fake whose SendMail outcome is configurable.
class FakeSmtp : public IMailProtocolPlugin {
public:
    bool succeed = true;
    int  sends = 0;
    UltraNetMailMessage last;

    std::string GetName() const override { return "FakeSMTP"; }
    std::string GetVersion() const override { return "0"; }
    std::vector<std::string> GetSupportedSchemes() const override { return {"smtp", "smtps"}; }
    UltraNetResult Initialize(const UltraNetConfig&) override { return UltraNetResult::Ok(); }
    void Shutdown() override {}
    UltraNetResult SendMail(const UltraNetMailMessage& m, const UltraNetMailOptions&) override {
        ++sends; last = m;
        return succeed ? UltraNetResult::Ok()
                       : UltraNetResult::Error(UltraNetResultCode::ConnectionRefused, "no route");
    }
    UltraNetResult FetchMessages(const std::string&, std::vector<UltraNetMailMessage>&,
                                 const UltraNetMailOptions&) override {
        return UltraNetResult::Ok();
    }
};

Draft SampleDraft() {
    Draft d;
    d.fromName = "Erika"; d.fromAddr = "erika@example.com";
    d.to = {"bob@example.com"};
    d.subject = "Hi"; d.body = "hello";
    Attachment a; a.filename = "n.txt"; a.mediaType = "text/plain"; a.data = {'h', 'i'};
    d.attachments.push_back(a);
    return d;
}

OutboxStore FreshOutbox(const std::string& tag) {
    OutboxStore s;
    REQUIRE(s.Open("outbox-" + tag, ":memory:").success);
    return s;
}

} // namespace

TEST(outbox_queue_and_pending_count) {
    OutboxStore store = FreshOutbox("queue");
    int64_t id = 0;
    REQUIRE(store.Enqueue("erika", "smtps://smtp.example.com:465/", SampleDraft(), id).success);
    REQUIRE(id > 0);

    int n = 0;
    REQUIRE(store.PendingCount(n).success);
    REQUIRE_EQ(n, 1);

    std::vector<OutboxItem> pending;
    REQUIRE(store.ListPending(pending).success);
    REQUIRE_EQ(pending.size(), (size_t)1);
    REQUIRE_EQ(pending[0].draft.to[0], std::string("bob@example.com"));
    REQUIRE_EQ(pending[0].draft.attachments.size(), (size_t)1);
    REQUIRE(pending[0].draft.attachments[0].data == std::vector<uint8_t>({'h', 'i'}));
}

TEST(outbox_flush_success_removes_items) {
    OutboxStore store = FreshOutbox("flushok");
    int64_t id = 0;
    store.Enqueue("erika", "smtps://x/", SampleDraft(), id);

    FakeSmtp smtp; smtp.succeed = true;
    Outbox outbox(store);
    auto stats = outbox.Flush(smtp, [](const std::string&) { return "pw"; });

    REQUIRE_EQ(stats.sent, 1);
    REQUIRE_EQ(stats.failed, 0);
    REQUIRE_EQ(smtp.sends, 1);
    REQUIRE_EQ(smtp.last.to[0], std::string("bob@example.com"));
    REQUIRE_EQ(smtp.last.attachments.size(), (size_t)1);   // attachment carried

    int n = 0; store.PendingCount(n);
    REQUIRE_EQ(n, 0);   // sent item removed
}

TEST(outbox_flush_failure_retains_and_counts_attempts) {
    OutboxStore store = FreshOutbox("flushfail");
    int64_t id = 0;
    store.Enqueue("erika", "smtps://x/", SampleDraft(), id);

    FakeSmtp smtp; smtp.succeed = false;
    Outbox outbox(store);
    auto stats = outbox.Flush(smtp, [](const std::string&) { return "pw"; });
    REQUIRE_EQ(stats.sent, 0);
    REQUIRE_EQ(stats.failed, 1);

    std::vector<OutboxItem> pending;
    store.ListPending(pending);
    REQUIRE_EQ(pending.size(), (size_t)1);       // retained for retry
    REQUIRE_EQ(pending[0].attempts, 1);          // attempt recorded
    REQUIRE(!pending[0].lastError.empty());

    // A later successful flush clears it.
    smtp.succeed = true;
    auto stats2 = outbox.Flush(smtp, [](const std::string&) { return "pw"; });
    REQUIRE_EQ(stats2.sent, 1);
    int n = 0; store.PendingCount(n);
    REQUIRE_EQ(n, 0);
}

// ---- SyncService end-to-end (fake mailbox) --------------------------------

namespace {
class FakeMailbox : public IMailboxProtocolPlugin {
public:
    std::string GetName() const override { return "FakeMailbox"; }
    std::string GetVersion() const override { return "0"; }
    std::vector<std::string> GetSupportedSchemes() const override { return {"imap", "imaps"}; }
    UltraNetResult Initialize(const UltraNetConfig&) override { return UltraNetResult::Ok(); }
    void Shutdown() override {}
    UltraNetResult SendMail(const UltraNetMailMessage&, const UltraNetMailOptions&) override {
        return UltraNetResult::Error(UltraNetResultCode::UnsupportedScheme, "fake");
    }
    UltraNetResult FetchMessages(const std::string&, std::vector<UltraNetMailMessage>&,
                                 const UltraNetMailOptions&) override { return UltraNetResult::Ok(); }
    UltraNetResult ListFolders(const std::string&, std::vector<UltraNetMailFolder>& out,
                               const UltraNetMailOptions&) override {
        UltraNetMailFolder f; f.name = "INBOX"; f.role = "inbox"; out = {f};
        return UltraNetResult::Ok();
    }
    UltraNetResult GetMailboxStatus(const std::string&, const std::string&,
                                    UltraNetMailboxStatus&, const UltraNetMailOptions&) override {
        return UltraNetResult::Ok();
    }
    UltraNetResult FetchEnvelopes(const std::string&, const std::string&, uint32_t sinceUid,
                                  std::vector<UltraNetMailEnvelope>& out,
                                  const UltraNetMailOptions&) override {
        out.clear();
        if (sinceUid < 1) {
            UltraNetMailEnvelope e; e.uid = 1; e.from = "Boss <boss@x.com>";
            e.to = {"erika@example.com"}; e.subject = "Hi"; e.messageId = "<1@x>";
            out.push_back(e);
        }
        return UltraNetResult::Ok();
    }
    UltraNetResult FetchMessage(const std::string&, const std::string&, uint32_t,
                                std::string& outRaw, const UltraNetMailOptions&) override {
        outRaw = "Subject: Hi\r\n\r\nbody";
        return UltraNetResult::Ok();
    }
    UltraNetResult StoreFlags(const std::string&, const std::string&, uint32_t,
                              UltraNetMailFlags, bool, const UltraNetMailOptions&) override {
        return UltraNetResult::Ok();
    }
    UltraNetResult MoveMessage(const std::string&, const std::string&, uint32_t,
                               const std::string&, const UltraNetMailOptions&) override {
        return UltraNetResult::Ok();
    }
    UltraNetResult AppendMessage(const std::string&, const std::string&, const std::string&,
                                 UltraNetMailFlags, const UltraNetMailOptions&) override {
        return UltraNetResult::Ok();
    }
};
} // namespace

TEST(syncservice_syncnow_populates_store) {
    LocalStore store;
    REQUIRE(store.Open("syncsvc", ":memory:").success);
    Account a; a.accountId = "erika"; a.email = "erika@example.com"; a.shortName = "erika";
    store.UpsertAccount(a);

    FakeMailbox mailbox;
    SyncService svc(store, mailbox, "/tmp/ultramail_syncsvc_test");
    UltraNetMailOptions opts;

    SyncOutcome r = svc.SyncNow("erika", "imaps://x/", opts);
    REQUIRE(r.ok);
    REQUIRE_EQ(r.stats.folders, 1);
    REQUIRE_EQ(r.stats.messages, 1);

    std::vector<MessageEnvelope> msgs;
    store.ListMessages("erika", "INBOX", 0, msgs);
    REQUIRE_EQ(msgs.size(), (size_t)1);
}
