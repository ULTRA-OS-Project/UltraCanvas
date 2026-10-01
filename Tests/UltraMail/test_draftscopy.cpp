// Tests/UltraMail/test_draftscopy.cpp
// A queued message is kept in the account's Drafts folder until it is sent:
// the copy is saved once (before the first attempt), stays while sending
// fails, and is deleted from Drafts once the message has gone out. A copy
// that cannot be saved does not stop the send. Replies keep their thread
// headers. Headless, with fake SMTP and IMAP plug-ins.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "UltraMailOutbox.h"

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetPlugins.h>

#include <string>
#include <vector>

using namespace UltraMail;

namespace {

class Smtp : public IMailProtocolPlugin {
public:
    bool succeed = false;
    int  sends = 0;
    UltraNetMailMessage last;
    std::string GetName() const override { return "Smtp"; }
    std::string GetVersion() const override { return "0"; }
    std::vector<std::string> GetSupportedSchemes() const override { return {"smtp"}; }
    UltraNetResult Initialize(const UltraNetConfig&) override { return UltraNetResult::Ok(); }
    void Shutdown() override {}
    UltraNetResult SendMail(const UltraNetMailMessage& m, const UltraNetMailOptions&) override {
        ++sends; last = m;
        return succeed ? UltraNetResult::Ok()
                       : UltraNetResult::Error(UltraNetResultCode::ConnectionRefused, "no route");
    }
    UltraNetResult FetchMessages(const std::string&, std::vector<UltraNetMailMessage>&,
                                 const UltraNetMailOptions&) override { return UltraNetResult::Ok(); }
};

// An IMAP server with a Drafts folder: APPEND stores the raw message under the
// next UID; FETCH ENVELOPE reads its Message-ID back; STORE +FLAGS \Deleted
// is recorded.
class Imap : public IMailboxProtocolPlugin {
public:
    struct Stored {
        uint32_t uid; std::string folder; std::string raw; bool deleted = false;
        UltraNetMailFlags flags = UltraNetMailFlags::None;
    };
    std::vector<Stored> messages;
    bool appendFails = false;
    std::string GetName() const override { return "Imap"; }
    std::string GetVersion() const override { return "0"; }
    std::vector<std::string> GetSupportedSchemes() const override { return {"imaps"}; }
    UltraNetResult Initialize(const UltraNetConfig&) override { return UltraNetResult::Ok(); }
    void Shutdown() override {}
    UltraNetResult SendMail(const UltraNetMailMessage&, const UltraNetMailOptions&) override {
        return UltraNetResult::Error(UltraNetResultCode::UnsupportedScheme, "imap");
    }
    UltraNetResult FetchMessages(const std::string&, std::vector<UltraNetMailMessage>&,
                                 const UltraNetMailOptions&) override { return UltraNetResult::Ok(); }
    UltraNetResult ListFolders(const std::string&, std::vector<UltraNetMailFolder>&,
                               const UltraNetMailOptions&) override { return UltraNetResult::Ok(); }
    UltraNetResult GetMailboxStatus(const std::string&, const std::string&, UltraNetMailboxStatus&,
                                    const UltraNetMailOptions&) override { return UltraNetResult::Ok(); }
    UltraNetResult FetchEnvelopes(const std::string&, const std::string& folder, uint32_t,
                                  std::vector<UltraNetMailEnvelope>& out,
                                  const UltraNetMailOptions&) override {
        out.clear();
        for (const auto& m : messages) {
            if (m.folder != folder) continue;
            UltraNetMailEnvelope e;
            e.uid = m.uid;
            e.messageId = Header(m.raw, "Message-ID");
            out.push_back(e);
        }
        // A message from another program in the same folder.
        UltraNetMailEnvelope other; other.uid = 900; other.messageId = "<other@example.com>";
        out.push_back(other);
        return UltraNetResult::Ok();
    }
    UltraNetResult FetchMessage(const std::string&, const std::string&, uint32_t, std::string&,
                                const UltraNetMailOptions&) override { return UltraNetResult::Ok(); }
    UltraNetResult StoreFlags(const std::string&, const std::string& folder, uint32_t uid,
                              UltraNetMailFlags flags, bool set, const UltraNetMailOptions&) override {
        if (uid == 900) otherTouched = true;
        for (auto& m : messages)
            if (m.folder == folder && m.uid == uid && set
                && (static_cast<uint32_t>(flags) & static_cast<uint32_t>(UltraNetMailFlags::Deleted)))
                m.deleted = true;
        return UltraNetResult::Ok();
    }
    UltraNetResult MoveMessage(const std::string&, const std::string&, uint32_t, const std::string&,
                               const UltraNetMailOptions&) override { return UltraNetResult::Ok(); }
    UltraNetResult AppendMessage(const std::string&, const std::string& folder, const std::string& raw,
                                 UltraNetMailFlags flags, const UltraNetMailOptions&) override {
        if (appendFails)
            return UltraNetResult::Error(UltraNetResultCode::ConnectionRefused, "offline");
        messages.push_back({static_cast<uint32_t>(messages.size() + 1), folder, raw, false, flags});
        return UltraNetResult::Ok();
    }
    bool otherTouched = false;

    static std::string Header(const std::string& raw, const std::string& name) {
        const std::string key = "\r\n" + name + ": ";
        auto at = ("\r\n" + raw).find(key);
        if (at == std::string::npos) return {};
        at += key.size() - 2;   // the "\r\n" prepended above
        auto end = raw.find("\r\n", at);
        return raw.substr(at, end - at);
    }
};

Draft Reply() {
    Draft d;
    d.fromName = "Erika"; d.fromAddr = "erika@example.com";
    d.to = {"anna@example.com"};
    d.bcc = {"boss@example.com"};
    d.subject = "Re: Notes";
    d.body = "Thanks!";
    d.inReplyTo = "<notes@example.com>";
    d.references = "<start@example.com> <notes@example.com>";
    return d;
}

ServerCopies Keeper(Imap& imap, const std::string& sentFolder = "Sent") {
    ServerCopies k;
    k.imap = &imap;
    k.prepare = [sentFolder](const std::string&, ServerFolders& out) {
        out.serverUrl = "imaps://mail.example.com/";
        out.draftsFolder = "Drafts";
        out.sentFolder = sentFolder;
        return UltraNetResult::Ok();
    };
    return k;
}

int Pending(OutboxStore& store) {
    int n = 0;
    store.PendingCount(n);
    return n;
}

} // namespace

TEST(new_message_ids_are_unique_and_carry_the_sender_domain) {
    const std::string a = NewMessageId("erika@example.com");
    const std::string b = NewMessageId("erika@example.com");
    REQUIRE(a != b);
    REQUIRE(a.front() == '<' && a.back() == '>');
    REQUIRE(a.find("@example.com>") != std::string::npos);
    REQUIRE(NewMessageId("").find("@ultramail.local>") != std::string::npos);
}

TEST(a_message_waits_in_drafts_until_it_is_sent) {
    OutboxStore store;
    REQUIRE(store.Open("drafts-wait", ":memory:").success);
    int64_t id = 0;
    REQUIRE(store.Enqueue("erika", "smtp://mail.example.com/", Reply(), id).success);

    Smtp smtp;
    Imap imap;
    const ServerCopies keeper = Keeper(imap);
    Outbox outbox(store);

    // First attempt fails: the copy is in Drafts, the message stays queued.
    Outbox::FlushStats first = outbox.Flush(smtp, nullptr, &keeper);
    REQUIRE_EQ(first.failed, 1);
    REQUIRE_EQ(first.draftsSaved, 1);
    REQUIRE_EQ(imap.messages.size(), std::size_t(1));
    REQUIRE_EQ(imap.messages[0].folder, std::string("Drafts"));
    const std::string& raw = imap.messages[0].raw;
    REQUIRE(raw.find("Subject: Re: Notes") != std::string::npos);
    REQUIRE(raw.find("In-Reply-To: <notes@example.com>") != std::string::npos);
    REQUIRE(raw.find("X-UltraMail-Outbox:") != std::string::npos);
    REQUIRE(raw.find("boss@example.com") == std::string::npos);   // Bcc stays private
    REQUIRE_EQ(Pending(store), 1);

    std::vector<OutboxItem> items;
    REQUIRE(store.ListPending(items).success);
    REQUIRE_EQ(items[0].draftsFolder, std::string("Drafts"));
    REQUIRE_EQ(Imap::Header(raw, "Message-ID"), items[0].messageId);

    // A second failed attempt does not save a second copy.
    outbox.Flush(smtp, nullptr, &keeper);
    REQUIRE_EQ(imap.messages.size(), std::size_t(1));
    REQUIRE(!imap.messages[0].deleted);

    // Sent: the copy leaves Drafts, nothing else there is touched, and the
    // message is filed in Sent - read, with the ID it was sent with.
    smtp.succeed = true;
    Outbox::FlushStats sent = outbox.Flush(smtp, nullptr, &keeper);
    REQUIRE_EQ(sent.sent, 1);
    REQUIRE_EQ(sent.draftFailures, 0);
    REQUIRE(imap.messages[0].deleted);
    REQUIRE(!imap.otherTouched);
    REQUIRE_EQ(Pending(store), 0);
    REQUIRE_EQ(sent.sentCopies, 1);
    REQUIRE_EQ(imap.messages.size(), std::size_t(2));
    REQUIRE_EQ(imap.messages[1].folder, std::string("Sent"));
    REQUIRE((static_cast<uint32_t>(imap.messages[1].flags)
             & static_cast<uint32_t>(UltraNetMailFlags::Seen)) != 0);
    REQUIRE_EQ(Imap::Header(imap.messages[1].raw, "Message-ID"), items[0].messageId);
    REQUIRE(imap.messages[1].raw.find("X-UltraMail-Outbox:") == std::string::npos);
    REQUIRE_EQ(smtp.last.headers["Message-ID"], items[0].messageId);
    // The Drafts copy asked to be a read draft.
    REQUIRE((static_cast<uint32_t>(imap.messages[0].flags)
             & static_cast<uint32_t>(UltraNetMailFlags::Draft)) != 0);
    // The reply went out in its thread.
    REQUIRE_EQ(smtp.last.headers["In-Reply-To"], std::string("<notes@example.com>"));
    REQUIRE_EQ(smtp.last.headers["References"],
               std::string("<start@example.com> <notes@example.com>"));
}

TEST(a_copy_that_cannot_be_saved_does_not_stop_the_send) {
    OutboxStore store;
    REQUIRE(store.Open("drafts-fail", ":memory:").success);
    int64_t id = 0;
    REQUIRE(store.Enqueue("erika", "smtp://mail.example.com/", Reply(), id).success);
    Smtp smtp;
    smtp.succeed = true;
    Imap imap;
    imap.appendFails = true;
    const ServerCopies keeper = Keeper(imap);
    Outbox::FlushStats stats = Outbox(store).Flush(smtp, nullptr, &keeper);
    REQUIRE_EQ(stats.sent, 1);
    REQUIRE_EQ(stats.draftsSaved, 0);
    REQUIRE_EQ(stats.draftFailures, 1);
    REQUIRE(!stats.lastDraftFailure);
    REQUIRE_EQ(Pending(store), 0);
}

TEST(drafts_copies_are_saved_without_a_send) {
    OutboxStore store;
    REQUIRE(store.Open("drafts-only", ":memory:").success);
    int64_t id = 0;
    REQUIRE(store.Enqueue("erika", "", Reply(), id).success);
    Imap imap;
    const ServerCopies keeper = Keeper(imap);
    Outbox outbox(store);
    REQUIRE_EQ(outbox.SaveDraftCopies(keeper).draftsSaved, 1);
    REQUIRE_EQ(outbox.SaveDraftCopies(keeper).draftsSaved, 0);   // once
    REQUIRE_EQ(imap.messages.size(), std::size_t(1));
    REQUIRE_EQ(Pending(store), 1);
}

TEST(without_a_drafts_keeper_flush_works_as_before) {
    OutboxStore store;
    REQUIRE(store.Open("drafts-none", ":memory:").success);
    int64_t id = 0;
    REQUIRE(store.Enqueue("erika", "smtp://mail.example.com/", Reply(), id).success);
    Smtp smtp;
    smtp.succeed = true;
    Outbox::FlushStats stats = Outbox(store).Flush(smtp, Outbox::OptionsResolver{});
    REQUIRE_EQ(stats.sent, 1);
    REQUIRE_EQ(stats.draftsSaved, 0);
    REQUIRE_EQ(Pending(store), 0);
}

TEST(the_outbox_retries_less_often_the_longer_it_fails) {
    OutboxRetryClock clock;
    REQUIRE(!clock.Scheduled());
    REQUIRE(!clock.Due(1000));
    clock.Failed(1000);
    REQUIRE_EQ(clock.NextAt(), int64_t(1060));      // 1 minute
    REQUIRE(!clock.Due(1059));
    REQUIRE(clock.Due(1060));
    clock.Failed(1060);
    REQUIRE_EQ(clock.NextAt(), int64_t(1180));      // 2 minutes
    clock.Failed(1180);
    REQUIRE_EQ(clock.NextAt(), int64_t(1480));      // 5 minutes
    clock.Failed(1480);
    REQUIRE_EQ(clock.NextAt(), int64_t(2080));      // 10 minutes
    clock.Failed(2080);
    REQUIRE_EQ(clock.NextAt(), int64_t(3880));      // then every 30
    clock.Failed(3880);
    REQUIRE_EQ(clock.NextAt(), int64_t(5680));
    REQUIRE_EQ(clock.Failures(), 6);
}

TEST(the_outbox_retries_soon_when_the_connection_is_back) {
    OutboxRetryClock clock;
    for (int i = 0; i < 6; ++i) clock.Failed(0);    // next pass 30 min out
    REQUIRE_EQ(clock.NextAt(), int64_t(1800));
    clock.RetryAt(10);                              // woke from sleep
    REQUIRE(clock.Due(10));
    clock.RetryAt(500);                             // never later than scheduled
    REQUIRE_EQ(clock.NextAt(), int64_t(10));
    REQUIRE_EQ(clock.Failures(), 6);                // the ladder is not reset
    clock.Succeeded();
    REQUIRE(!clock.Scheduled());
    REQUIRE_EQ(clock.Failures(), 0);
    clock.Failed(100);
    REQUIRE_EQ(clock.NextAt(), int64_t(160));       // back to 1 minute
}

TEST(no_sent_copy_where_the_server_files_sent_mail_itself) {
    OutboxStore store;
    REQUIRE(store.Open("sent-none", ":memory:").success);
    int64_t id = 0;
    REQUIRE(store.Enqueue("erika", "smtp://mail.example.com/", Reply(), id).success);
    Smtp smtp;
    smtp.succeed = true;
    Imap imap;
    const ServerCopies keeper = Keeper(imap, /*sentFolder=*/"");
    Outbox::FlushStats stats = Outbox(store).Flush(smtp, nullptr, &keeper);
    REQUIRE_EQ(stats.sent, 1);
    REQUIRE_EQ(stats.sentCopies, 0);
    REQUIRE_EQ(stats.sentCopyFailures, 0);
    REQUIRE_EQ(imap.messages.size(), std::size_t(1));   // only the Drafts copy
    REQUIRE(imap.messages[0].deleted);

    REQUIRE(ServerFilesSentMail("imap.gmail.com"));
    REQUIRE(ServerFilesSentMail("outlook.office365.com"));
    REQUIRE(ServerFilesSentMail("IMAP-MAIL.OUTLOOK.COM"));
    REQUIRE(!ServerFilesSentMail("imap.example.com"));
    REQUIRE(!ServerFilesSentMail("mail.notgmail.com.example"));
}

TEST(a_waiting_message_can_be_deleted_with_its_drafts_copy) {
    OutboxStore store;
    REQUIRE(store.Open("outbox-delete", ":memory:").success);
    int64_t keep = 0, drop = 0;
    REQUIRE(store.Enqueue("erika", "smtp://x/", Reply(), keep).success);
    REQUIRE(store.Enqueue("erika", "smtp://x/", Reply(), drop).success);
    Imap imap;
    const ServerCopies keeper = Keeper(imap);
    Outbox outbox(store);
    REQUIRE_EQ(outbox.SaveDraftCopies(keeper).draftsSaved, 2);

    REQUIRE(outbox.DeleteMessage(drop, &keeper).success);
    std::vector<OutboxItem> items;
    REQUIRE(store.ListPending(items).success);
    REQUIRE_EQ(items.size(), std::size_t(1));
    REQUIRE_EQ(items[0].id, keep);
    REQUIRE(!imap.messages[0].deleted);   // the other message's copy stays
    REQUIRE(imap.messages[1].deleted);
    REQUIRE(outbox.DeleteMessage(drop, &keeper).success);   // already gone: fine
}
