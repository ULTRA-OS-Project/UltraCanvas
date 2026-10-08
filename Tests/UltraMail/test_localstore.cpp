// Tests/UltraMail/test_localstore.cpp
// Exercises the UltraMail LocalStore against an in-memory UltraDatabase:
// schema/migrations, accounts, folders, message upserts, the needs-answer
// eligibility rules, flag updates, and the per-account status rollup that
// drives the info-tile bar.
// Version: 0.4.0 - the verified sender domain with the verdict; ListStaleVerdicts
// Version: 0.3.0 - WeighSentRecipients
// Version: 0.2.0 - the needs-answer rules (age, people written to)
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "UltraMailLocalStore.h"
#include "UltraCanvasPathUtf8.h"

#include <UltraDatabase/UltraDatabase.h>

#include <ctime>
#include <filesystem>
#include <map>
#include <string>

using namespace UltraMail;

namespace {

// Fresh in-memory store under a unique connection name.
LocalStore FreshStore(const std::string& tag) {
    LocalStore store;
    UltraDbResult r = store.Open("umtest-" + tag, ":memory:");
    REQUIRE(r.success);
    return store;
}

void AddAccountWithInbox(LocalStore& s, const std::string& id,
                         const std::string& email, const std::string& shortName) {
    Account a; a.accountId = id; a.email = email; a.shortName = shortName;
    a.displayName = shortName;
    REQUIRE(s.UpsertAccount(a).success);
    Folder f; f.accountId = id; f.name = "INBOX"; f.role = FolderRole::Inbox;
    REQUIRE(s.UpsertFolder(f).success);
}

} // namespace

TEST(accounts_store_their_server_settings) {
    LocalStore s = FreshStore("servers");
    Account a; a.accountId = "erika"; a.email = "erika@example.org"; a.shortName = "erika";
    a.displayName = "Erika";
    REQUIRE(!a.HasServers());
    REQUIRE(s.UpsertAccount(a).success);

    // Stored without servers: read back the same way (falls back to presets).
    std::vector<Account> accs;
    REQUIRE(s.ListAccounts(accs).success);
    REQUIRE_EQ(accs.size(), std::size_t(1));
    REQUIRE(!accs[0].HasServers());

    a.imap.host = "mail.example.org"; a.imap.port = 993;
    a.imap.security = MailSecurity::SslTls; a.imap.username = "erika"; a.imap.oauth = false;
    a.smtp.host = "mail.example.org"; a.smtp.port = 587;
    a.smtp.security = MailSecurity::StartTls; a.smtp.username = "erika@example.org";
    a.providerName = "Example Org";
    REQUIRE(a.HasServers());
    REQUIRE(s.UpsertAccount(a).success);   // update in place

    REQUIRE(s.ListAccounts(accs).success);
    REQUIRE_EQ(accs.size(), std::size_t(1));
    const Account& b = accs[0];
    REQUIRE(b.HasServers());
    REQUIRE_EQ(b.imap.host, std::string("mail.example.org"));
    REQUIRE_EQ(b.imap.port, 993);
    REQUIRE(b.imap.security == MailSecurity::SslTls);
    REQUIRE_EQ(b.imap.username, std::string("erika"));
    REQUIRE_EQ(b.smtp.host, std::string("mail.example.org"));
    REQUIRE_EQ(b.smtp.port, 587);
    REQUIRE(b.smtp.security == MailSecurity::StartTls);
    REQUIRE_EQ(b.smtp.username, std::string("erika@example.org"));
    REQUIRE_EQ(b.providerName, std::string("Example Org"));
    REQUIRE(b.imap.auth == UltraNetMailAuth::Any);   // never set: Automatic
    REQUIRE(b.smtp.auth == UltraNetMailAuth::Any);

    a.imap.auth = UltraNetMailAuth::Password;
    a.smtp.auth = UltraNetMailAuth::None;
    REQUIRE(s.UpsertAccount(a).success);
    REQUIRE(s.ListAccounts(accs).success);
    REQUIRE(accs[0].imap.auth == UltraNetMailAuth::Password);
    REQUIRE(accs[0].smtp.auth == UltraNetMailAuth::None);
}

namespace {

MessageEnvelope Incoming(const std::string& acc, int64_t uid, const std::string& from,
                         const std::vector<std::string>& to) {
    MessageEnvelope m;
    m.accountId = acc; m.folder = "INBOX"; m.uid = uid;
    m.fromAddr = from; m.to = to; m.subject = "s" + std::to_string(uid);
    m.date = 1000 + uid;
    return m;
}

int NeedsFor(LocalStore& s, const std::string& acc) {
    std::vector<AccountStatus> st;
    REQUIRE(s.GetAccountStatus(st).success);
    for (auto& x : st) if (x.accountId == acc) return x.needsAnswer;
    return -1;
}

int UnreadFor(LocalStore& s, const std::string& acc) {
    std::vector<AccountStatus> st;
    REQUIRE(s.GetAccountStatus(st).success);
    for (auto& x : st) if (x.accountId == acc) return x.unread;
    return -1;
}

} // namespace

TEST(accounts_roundtrip) {
    LocalStore s = FreshStore("acc");
    AddAccountWithInbox(s, "erika", "erika@example.com", "erika");
    std::vector<Account> accs;
    REQUIRE(s.ListAccounts(accs).success);
    REQUIRE_EQ(accs.size(), (size_t)1);
    REQUIRE_EQ(accs[0].email, std::string("erika@example.com"));
    REQUIRE_EQ(accs[0].shortName, std::string("erika"));
}

TEST(folders_roundtrip) {
    LocalStore s = FreshStore("fold");
    AddAccountWithInbox(s, "erika", "erika@example.com", "erika");
    Folder sent; sent.accountId = "erika"; sent.name = "Sent"; sent.role = FolderRole::Sent;
    REQUIRE(s.UpsertFolder(sent).success);
    std::vector<Folder> fs;
    REQUIRE(s.ListFolders("erika", fs).success);
    REQUIRE_EQ(fs.size(), (size_t)2);
}

TEST(folders_persist_selectable_flag) {
    LocalStore s = FreshStore("selectable");
    AddAccountWithInbox(s, "erika", "erika@example.com", "erika");
    // A \Noselect container (e.g. Gmail's "[Gmail]") and a real folder under it.
    Folder container; container.accountId = "erika"; container.name = "[Gmail]";
    container.role = FolderRole::Normal; container.selectable = false;
    REQUIRE(s.UpsertFolder(container).success);
    Folder sent; sent.accountId = "erika"; sent.name = "[Gmail]/Sent Mail";
    sent.role = FolderRole::Sent; sent.selectable = true;
    REQUIRE(s.UpsertFolder(sent).success);

    std::vector<Folder> fs;
    REQUIRE(s.ListFolders("erika", fs).success);
    bool sawContainer = false, sawSent = false, sawInbox = false;
    for (const auto& f : fs) {
        if (f.name == "[Gmail]")           { sawContainer = true; REQUIRE(!f.selectable); }
        if (f.name == "[Gmail]/Sent Mail") { sawSent = true;      REQUIRE(f.selectable); }
        if (f.name == "INBOX")             { sawInbox = true;     REQUIRE(f.selectable); }
    }
    REQUIRE(sawContainer);
    REQUIRE(sawSent);
    REQUIRE(sawInbox);
}

TEST(messages_list_recent_first) {
    LocalStore s = FreshStore("msglist");
    AddAccountWithInbox(s, "erika", "erika@example.com", "erika");
    REQUIRE(s.UpsertMessage(Incoming("erika", 1, "a@x.com", {"erika@example.com"})).success);
    REQUIRE(s.UpsertMessage(Incoming("erika", 2, "b@x.com", {"erika@example.com"})).success);
    std::vector<MessageEnvelope> msgs;
    REQUIRE(s.ListMessages("erika", "INBOX", 0, msgs).success);
    REQUIRE_EQ(msgs.size(), (size_t)2);
    REQUIRE_EQ(msgs[0].uid, (int64_t)2);   // most recent first
    REQUIRE_EQ(msgs[1].uid, (int64_t)1);
    REQUIRE_EQ(msgs[0].to.size(), (size_t)1);
    REQUIRE_EQ(msgs[0].to[0], std::string("erika@example.com"));
}

TEST(needs_answer_direct_incoming_counts) {
    LocalStore s = FreshStore("na1");
    AddAccountWithInbox(s, "erika", "erika@example.com", "erika");
    // Addressed directly to the user, unanswered -> needs answer.
    REQUIRE(s.UpsertMessage(Incoming("erika", 1, "boss@x.com", {"erika@example.com"})).success);
    REQUIRE_EQ(NeedsFor(s, "erika"), 1);

    std::vector<MessageEnvelope> na;
    REQUIRE(s.ListNeedsAnswer("erika", na).success);
    REQUIRE_EQ(na.size(), (size_t)1);
}

TEST(needs_answer_excludes_answered_automated_and_bulk) {
    LocalStore s = FreshStore("na2");
    AddAccountWithInbox(s, "erika", "erika@example.com", "erika");

    // Already answered -> excluded.
    MessageEnvelope answered = Incoming("erika", 1, "boss@x.com", {"erika@example.com"});
    answered.flags = Flag_Answered;
    REQUIRE(s.UpsertMessage(answered).success);

    // Automated / bulk -> excluded.
    MessageEnvelope bulk = Incoming("erika", 2, "news@x.com", {"erika@example.com"});
    bulk.automated = true;
    REQUIRE(s.UpsertMessage(bulk).success);

    // Not addressed to the user (bcc/list) -> excluded.
    REQUIRE(s.UpsertMessage(Incoming("erika", 3, "x@x.com", {"someone@else.com"})).success);

    // Sent by the user themselves -> excluded.
    REQUIRE(s.UpsertMessage(Incoming("erika", 4, "erika@example.com", {"erika@example.com"})).success);

    REQUIRE_EQ(NeedsFor(s, "erika"), 0);
}

TEST(mark_answered_drops_needs_answer) {
    LocalStore s = FreshStore("na3");
    AddAccountWithInbox(s, "erika", "erika@example.com", "erika");
    REQUIRE(s.UpsertMessage(Incoming("erika", 7, "boss@x.com", {"erika@example.com"})).success);
    REQUIRE_EQ(NeedsFor(s, "erika"), 1);

    REQUIRE(s.MarkAnswered("erika", "INBOX", 7).success);
    REQUIRE_EQ(NeedsFor(s, "erika"), 0);

    // Clearing \Answered brings it back (still eligible).
    REQUIRE(s.SetFlags("erika", "INBOX", 7, Flag_Answered, false).success);
    REQUIRE_EQ(NeedsFor(s, "erika"), 1);
}

TEST(needs_answer_manual_mark_overrides_rule) {
    LocalStore s = FreshStore("na4");
    AddAccountWithInbox(s, "erika", "erika@example.com", "erika");
    // Not addressed to the user: the rule leaves it off the list.
    REQUIRE(s.UpsertMessage(Incoming("erika", 1, "list@x.com", {"team@x.com"})).success);
    // Addressed to the user: the rule puts it on.
    REQUIRE(s.UpsertMessage(Incoming("erika", 2, "boss@x.com", {"erika@example.com"})).success);
    REQUIRE_EQ(NeedsFor(s, "erika"), 1);

    REQUIRE(s.SetNeedsAnswer("erika", "INBOX", 1, true).success);    // on, by choice
    REQUIRE(s.SetNeedsAnswer("erika", "INBOX", 2, false).success);   // off, by choice
    std::vector<MessageEnvelope> na;
    REQUIRE(s.ListNeedsAnswer("erika", na).success);
    REQUIRE_EQ(na.size(), (size_t)1);
    REQUIRE_EQ(na[0].uid, (int64_t)1);

    // A header re-sync keeps both choices.
    REQUIRE(s.UpsertMessage(Incoming("erika", 1, "list@x.com", {"team@x.com"})).success);
    REQUIRE(s.UpsertMessage(Incoming("erika", 2, "boss@x.com", {"erika@example.com"})).success);
    REQUIRE(s.ListNeedsAnswer("erika", na).success);
    REQUIRE_EQ(na.size(), (size_t)1);
    REQUIRE_EQ(na[0].uid, (int64_t)1);

    // Answering settles the "needs an answer" choice for good: clearing
    // \Answered again falls back to the rule, which leaves it off.
    REQUIRE(s.MarkAnswered("erika", "INBOX", 1).success);
    REQUIRE_EQ(NeedsFor(s, "erika"), 0);
    REQUIRE(s.SetFlags("erika", "INBOX", 1, Flag_Answered, false).success);
    REQUIRE_EQ(NeedsFor(s, "erika"), 0);

    REQUIRE(!s.SetNeedsAnswer("erika", "INBOX", 99, true).success);   // unknown message
}

TEST(needs_answer_rules_age_limit) {
    LocalStore s = FreshStore("narules-age");
    AddAccountWithInbox(s, "erika", "erika@example.com", "erika");
    const int64_t now = static_cast<int64_t>(std::time(nullptr));
    MessageEnvelope recent = Incoming("erika", 1, "boss@x.com", {"erika@example.com"});
    recent.date = now - 3 * 86400;
    MessageEnvelope old = Incoming("erika", 2, "boss@x.com", {"erika@example.com"});
    old.date = now - 60 * 86400;
    REQUIRE(s.UpsertMessage(recent).success);
    REQUIRE(s.UpsertMessage(old).success);
    REQUIRE_EQ(NeedsFor(s, "erika"), 2);                  // no rules: both

    NeedsAnswerRules rules; rules.maxAgeDays = 14;
    s.SetNeedsAnswerRules(rules);
    REQUIRE_EQ(NeedsFor(s, "erika"), 1);
    std::vector<MessageEnvelope> na;
    REQUIRE(s.ListNeedsAnswer("erika", na).success);
    REQUIRE_EQ(na.size(), (size_t)1);
    REQUIRE_EQ(na[0].uid, (int64_t)1);

    // The user's own mark counts whatever its age.
    REQUIRE(s.SetNeedsAnswer("erika", "INBOX", 2, true).success);
    REQUIRE_EQ(NeedsFor(s, "erika"), 2);
}

TEST(needs_answer_rules_only_people_written_to) {
    LocalStore s = FreshStore("narules-sent");
    AddAccountWithInbox(s, "erika", "erika@example.com", "erika");
    REQUIRE(s.UpsertMessage(Incoming("erika", 1, "Boss@X.com", {"erika@example.com"})).success);
    REQUIRE(s.UpsertMessage(Incoming("erika", 2, "stranger@y.com", {"erika@example.com"})).success);

    NeedsAnswerRules rules; rules.onlyWrittenTo = true;
    s.SetNeedsAnswerRules(rules);
    // No Sent mail stored yet: the rule cannot tell, so it narrows nothing.
    REQUIRE_EQ(NeedsFor(s, "erika"), 2);

    Folder sent; sent.accountId = "erika"; sent.name = "Sent"; sent.role = FolderRole::Sent;
    REQUIRE(s.UpsertFolder(sent).success);
    MessageEnvelope mine; mine.accountId = "erika"; mine.folder = "Sent"; mine.uid = 1;
    mine.fromAddr = "erika@example.com"; mine.to = {"colleague@z.com", "boss@x.com"};
    mine.date = 900;
    REQUIRE(s.UpsertMessage(mine).success);
    // Only the sender written to (address compared without case) is waiting.
    REQUIRE_EQ(NeedsFor(s, "erika"), 1);
    std::vector<MessageEnvelope> na;
    REQUIRE(s.ListNeedsAnswer("erika", na).success);
    REQUIRE_EQ(na.size(), (size_t)1);
    REQUIRE_EQ(na[0].uid, (int64_t)1);
}

// Mail is addressed "Name <address>" far more often than bare - by the
// composer's completion and by every other mail program. Such a recipient is
// written to as much as a bare one; and the set follows new Sent mail.
TEST(needs_answer_rules_written_to_reads_named_recipients) {
    LocalStore s = FreshStore("narules-named");
    AddAccountWithInbox(s, "erika", "erika@example.com", "erika");
    REQUIRE(s.UpsertMessage(Incoming("erika", 1, "maya@gmail.com", {"erika@example.com"})).success);
    REQUIRE(s.UpsertMessage(Incoming("erika", 2, "ravi@x.com", {"erika@example.com"})).success);
    REQUIRE(s.UpsertMessage(Incoming("erika", 3, "noreply@shop.com", {"erika@example.com"})).success);
    NeedsAnswerRules rules; rules.onlyWrittenTo = true;
    s.SetNeedsAnswerRules(rules);

    Folder sent; sent.accountId = "erika"; sent.name = "Sent"; sent.role = FolderRole::Sent;
    REQUIRE(s.UpsertFolder(sent).success);
    MessageEnvelope mine; mine.accountId = "erika"; mine.folder = "Sent"; mine.uid = 1;
    mine.fromAddr = "erika@example.com"; mine.to = {"Maya Bennett <Maya@Gmail.com>"};
    mine.date = 900;
    REQUIRE(s.UpsertMessage(mine).success);
    REQUIRE_EQ(NeedsFor(s, "erika"), 1);                      // Maya only
    std::vector<MessageEnvelope> na;
    REQUIRE(s.ListNeedsAnswer("erika", na).success);
    REQUIRE_EQ(na.size(), (size_t)1);
    REQUIRE_EQ(na[0].uid, (int64_t)1);

    // A reply to Ravi lands in Sent: he counts from then on.
    mine.uid = 2; mine.to = {"\"Singh, Ravi\" <ravi@x.com>", "team@x.com"};
    REQUIRE(s.UpsertMessage(mine).success);
    REQUIRE_EQ(NeedsFor(s, "erika"), 2);
    REQUIRE(s.ListNeedsAnswer("erika", na).success);
    REQUIRE_EQ(na.size(), (size_t)2);

    // The user's own mark counts whoever sent it.
    REQUIRE(s.SetNeedsAnswer("erika", "INBOX", 3, true).success);
    REQUIRE_EQ(NeedsFor(s, "erika"), 3);
}

TEST(unread_counts_inbox_unseen) {
    LocalStore s = FreshStore("unread");
    AddAccountWithInbox(s, "erika", "erika@example.com", "erika");
    REQUIRE(s.UpsertMessage(Incoming("erika", 1, "a@x.com", {"erika@example.com"})).success);
    REQUIRE(s.UpsertMessage(Incoming("erika", 2, "b@x.com", {"erika@example.com"})).success);
    REQUIRE_EQ(UnreadFor(s, "erika"), 2);

    REQUIRE(s.SetFlags("erika", "INBOX", 1, Flag_Seen, true).success);
    REQUIRE_EQ(UnreadFor(s, "erika"), 1);
}

TEST(unread_split_today_vs_older) {
    LocalStore s = FreshStore("split");
    AddAccountWithInbox(s, "erika", "erika@example.com", "erika");
    const int64_t midnight = 1'000'000;
    // Two unread today, one unread older, one read today (not counted).
    MessageEnvelope a = Incoming("erika", 1, "a@x.com", {"erika@example.com"}); a.date = midnight + 10;
    MessageEnvelope b = Incoming("erika", 2, "b@x.com", {"erika@example.com"}); b.date = midnight;
    MessageEnvelope c = Incoming("erika", 3, "c@x.com", {"erika@example.com"}); c.date = midnight - 1;
    MessageEnvelope d = Incoming("erika", 4, "d@x.com", {"erika@example.com"}); d.date = midnight + 5;
    d.flags = Flag_Seen;
    for (auto* m : {&a, &b, &c, &d}) REQUIRE(s.UpsertMessage(*m).success);

    std::vector<AccountStatus> st;
    REQUIRE(s.GetAccountStatus(st, midnight).success);
    REQUIRE_EQ(st.size(), (size_t)1);
    REQUIRE_EQ(st[0].email, std::string("erika@example.com"));
    REQUIRE_EQ(st[0].unread, 3);
    REQUIRE_EQ(st[0].unreadToday, 2);
    REQUIRE_EQ(st[0].unreadOlder, 1);
}

TEST(deleted_excluded_from_rollups) {
    LocalStore s = FreshStore("del");
    AddAccountWithInbox(s, "erika", "erika@example.com", "erika");
    REQUIRE(s.UpsertMessage(Incoming("erika", 1, "boss@x.com", {"erika@example.com"})).success);
    REQUIRE_EQ(NeedsFor(s, "erika"), 1);
    REQUIRE_EQ(UnreadFor(s, "erika"), 1);

    REQUIRE(s.SetFlags("erika", "INBOX", 1, Flag_Deleted, true).success);
    REQUIRE_EQ(NeedsFor(s, "erika"), 0);
    REQUIRE_EQ(UnreadFor(s, "erika"), 0);
}

TEST(multi_account_status_rollup) {
    LocalStore s = FreshStore("multi");
    AddAccountWithInbox(s, "erika", "erika@example.com", "erika");
    AddAccountWithInbox(s, "work",  "e@work.com",       "work");

    // erika: 2 unread, 1 needs answer.
    REQUIRE(s.UpsertMessage(Incoming("erika", 1, "boss@x.com", {"erika@example.com"})).success);
    MessageEnvelope seen2 = Incoming("erika", 2, "n@x.com", {"someone@else.com"});
    REQUIRE(s.UpsertMessage(seen2).success);  // unread but not needs-answer

    // work: 1 unread, 1 needs answer.
    REQUIRE(s.UpsertMessage(Incoming("work", 1, "client@x.com", {"e@work.com"})).success);

    std::vector<AccountStatus> st;
    REQUIRE(s.GetAccountStatus(st).success);
    REQUIRE_EQ(st.size(), (size_t)2);

    REQUIRE_EQ(NeedsFor(s, "erika"), 1);
    REQUIRE_EQ(UnreadFor(s, "erika"), 2);
    REQUIRE_EQ(NeedsFor(s, "work"), 1);
    REQUIRE_EQ(UnreadFor(s, "work"), 1);
}

TEST(upsert_is_idempotent) {
    LocalStore s = FreshStore("idem");
    AddAccountWithInbox(s, "erika", "erika@example.com", "erika");
    MessageEnvelope m = Incoming("erika", 1, "boss@x.com", {"erika@example.com"});
    REQUIRE(s.UpsertMessage(m).success);
    REQUIRE(s.UpsertMessage(m).success);  // same uid again
    std::vector<MessageEnvelope> msgs;
    s.ListMessages("erika", "INBOX", 0, msgs);
    REQUIRE_EQ(msgs.size(), (size_t)1);   // no duplicate
    REQUIRE_EQ(NeedsFor(s, "erika"), 1);
}

TEST(security_verdicts_round_trip_and_survive_envelope_upserts) {
    LocalStore s = FreshStore("security");
    AddAccountWithInbox(s, "erika", "erika@example.com", "erika");

    MessageEnvelope m;
    m.accountId = "erika"; m.folder = "INBOX"; m.uid = 7;
    m.fromAddr = "service@paypa1-secure.example"; m.subject = "Your account is locked";
    REQUIRE(s.UpsertMessage(m).success);

    // Nothing stored yet reads as "unscanned", not as "clean".
    MessageSecurity none;
    REQUIRE(s.GetSecurity("erika", "INBOX", 7, none).success);
    REQUIRE(!none.Scanned());
    REQUIRE(none.level == ThreatLevel::Unscanned);

    MessageSecurity sec;
    sec.level  = ThreatLevel::Scam;
    sec.score  = 60;
    sec.reason = "A link reads \"paypal.com\" but goes to 198.51.100.7.";
    REQUIRE(s.SetSecurity("erika", "INBOX", 7, sec).success);

    MessageSecurity read;
    REQUIRE(s.GetSecurity("erika", "INBOX", 7, read).success);
    REQUIRE(read.level == ThreatLevel::Scam);
    REQUIRE_EQ(read.score, 60);
    REQUIRE_EQ(read.reason, sec.reason);
    REQUIRE(read.scannedAt > 0);

    // The reason the verdict lives in its own table: a later header sync
    // re-upserts the envelope, and the scan must not be reset by it.
    m.flags = Flag_Seen;
    REQUIRE(s.UpsertMessage(m).success);
    REQUIRE(s.GetSecurity("erika", "INBOX", 7, read).success);
    REQUIRE(read.level == ThreatLevel::Scam);

    // The message list reads a whole folder's verdicts in one query.
    std::map<int64_t, MessageSecurity> all;
    REQUIRE(s.ListSecurity("erika", "INBOX", all).success);
    REQUIRE_EQ(all.size(), (size_t)1);
    REQUIRE(all[7].level == ThreatLevel::Scam);

    // Removing the account takes its verdicts with it.
    REQUIRE(s.RemoveAccount("erika").success);
    REQUIRE(s.ListSecurity("erika", "INBOX", all).success);
    REQUIRE(all.empty());
}

TEST(attachment_count_kept_beside_the_scan_verdict) {
    LocalStore s = FreshStore("attcount");
    AddAccountWithInbox(s, "erika", "erika@example.com", "erika");
    for (int64_t uid : {1, 2, 3}) {
        MessageEnvelope m = Incoming("erika", uid, "a@x.com", {"erika@example.com"});
        m.date = 100 + uid;
        REQUIRE(s.UpsertMessage(m).success);
    }
    // Nothing counted yet: all three, newest first.
    std::vector<int64_t> uncounted;
    REQUIRE(s.ListUncountedAttachments("erika", "INBOX", 10, uncounted).success);
    REQUIRE_EQ(uncounted.size(), (size_t)3);
    REQUIRE_EQ(uncounted[0], (int64_t)3);

    // A count without a verdict, then a verdict without a count: both kept.
    REQUIRE(s.SetAttachmentCount("erika", "INBOX", 2, 4).success);
    MessageSecurity verdict;
    verdict.level = ThreatLevel::Clean;
    REQUIRE(s.SetSecurity("erika", "INBOX", 2, verdict).success);   // attachments = -1
    MessageSecurity got;
    REQUIRE(s.GetSecurity("erika", "INBOX", 2, got).success);
    REQUIRE_EQ(got.attachments, 4);
    REQUIRE(got.level == ThreatLevel::Clean);

    // A verdict with a count sets it.
    verdict.attachments = 0;
    REQUIRE(s.SetSecurity("erika", "INBOX", 1, verdict).success);
    REQUIRE(s.ListUncountedAttachments("erika", "INBOX", 10, uncounted).success);
    REQUIRE_EQ(uncounted.size(), (size_t)1);
    REQUIRE_EQ(uncounted[0], (int64_t)3);

    std::map<int64_t, MessageSecurity> all;
    REQUIRE(s.ListSecurity("erika", "INBOX", all).success);
    REQUIRE_EQ(all[1].attachments, 0);
    REQUIRE_EQ(all[2].attachments, 4);
}

TEST(a_verdict_keeps_its_finding_codes) {
    LocalStore s = FreshStore("findings");
    AddAccountWithInbox(s, "erika", "erika@example.com", "erika");
    REQUIRE(s.UpsertMessage(Incoming("erika", 1, "a@x.com", {"erika@example.com"})).success);
    MessageSecurity verdict;
    verdict.level = ThreatLevel::Scam;
    verdict.findings = "romance-scam,crypto-content";
    REQUIRE(s.SetSecurity("erika", "INBOX", 1, verdict).success);
    MessageSecurity got;
    REQUIRE(s.GetSecurity("erika", "INBOX", 1, got).success);
    REQUIRE_EQ(got.findings, std::string("romance-scam,crypto-content"));
    REQUIRE(got.HasFinding("romance-scam"));
    REQUIRE(got.HasFinding("crypto-content"));
    REQUIRE(!got.HasFinding("crypto"));          // a whole code, not a part of one
    std::map<int64_t, MessageSecurity> all;
    REQUIRE(s.ListSecurity("erika", "INBOX", all).success);
    REQUIRE(all[1].HasFinding("romance-scam"));
}

TEST(verified_sender_and_stale_verdicts) {
    LocalStore s = FreshStore("verified");
    AddAccountWithInbox(s, "erika", "erika@example.com", "erika");
    for (int64_t uid : {1, 2, 3}) {
        MessageEnvelope m = Incoming("erika", uid, "a@x.com", {"erika@example.com"});
        m.date = 100 + uid;
        REQUIRE(s.UpsertMessage(m).success);
    }
    MessageSecurity verdict;
    verdict.level = ThreatLevel::Clean;
    verdict.verifiedDomain = "shop.example";
    verdict.verifiedBy = "DKIM signature and DMARC";
    verdict.scannedAt = 1000;                         // older rules
    REQUIRE(s.SetSecurity("erika", "INBOX", 1, verdict).success);
    verdict.scannedAt = 1000;
    REQUIRE(s.SetSecurity("erika", "INBOX", 3, verdict).success);
    verdict.verifiedDomain.clear();
    verdict.verifiedBy.clear();
    verdict.scannedAt = 5000;                         // current rules
    REQUIRE(s.SetSecurity("erika", "INBOX", 2, verdict).success);

    MessageSecurity got;
    REQUIRE(s.GetSecurity("erika", "INBOX", 1, got).success);
    REQUIRE_EQ(got.verifiedDomain, std::string("shop.example"));
    REQUIRE_EQ(got.verifiedBy, std::string("DKIM signature and DMARC"));
    std::map<int64_t, MessageSecurity> all;
    REQUIRE(s.ListSecurity("erika", "INBOX", all).success);
    REQUIRE(all[2].verifiedDomain.empty());

    // Scanned before revision 2000: 1 and 3, newest first; not 2.
    std::vector<int64_t> stale;
    REQUIRE(s.ListStaleVerdicts("erika", "INBOX", 2000, 10, stale).success);
    REQUIRE_EQ(stale.size(), (size_t)2);
    REQUIRE_EQ(stale[0], (int64_t)3);
    REQUIRE_EQ(stale[1], (int64_t)1);
    // A row holding only an attachment count was never scanned: not stale.
    REQUIRE(s.SetAttachmentCount("erika", "INBOX", 4, 1).success);
    REQUIRE(s.ListStaleVerdicts("erika", "INBOX", 2000, 10, stale).success);
    REQUIRE_EQ(stale.size(), (size_t)2);
}

// UltraMail opens mail.db twice: the UI thread's connection and the sync
// workers'. In WAL mode the UI's reads never queue behind a sync's writes (a
// shared connection made switching accounts mid-sync take 10-20 seconds), and
// each connection sees what the other committed.
TEST(file_store_uses_wal_and_shares_rows_across_connections) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "ultramail_wal_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const std::string db = UltraCanvas::PathToUtf8(dir / "mail.db");

    LocalStore ui, worker;
    REQUIRE(ui.Open("umtest-wal-ui", db).success);
    REQUIRE(worker.Open("umtest-wal-worker", db).success);

    UltraDbResultSet rs;
    REQUIRE(UltraDb_Query("umtest-wal-ui", "PRAGMA journal_mode", rs).success);
    REQUIRE_EQ(rs.Size(), (size_t)1);
    REQUIRE_EQ(rs.Row(0)[0].AsString(), std::string("wal"));

    AddAccountWithInbox(worker, "erika", "erika@example.org", "erika");
    MessageEnvelope m;
    m.accountId = "erika"; m.folder = "INBOX"; m.uid = 7; m.subject = "Hello";
    REQUIRE(worker.UpsertMessage(m).success);

    std::vector<Account> accs;
    REQUIRE(ui.ListAccounts(accs).success);
    REQUIRE_EQ(accs.size(), (size_t)1);
    std::vector<MessageEnvelope> msgs;
    REQUIRE(ui.ListMessages("erika", "INBOX", 0, msgs).success);
    REQUIRE_EQ(msgs.size(), (size_t)1);
    REQUIRE_EQ(msgs[0].uid, (int64_t)7);

    UltraDb_CloseConnection("umtest-wal-ui");
    UltraDb_CloseConnection("umtest-wal-worker");
    fs::remove_all(dir, ec);
}

TEST(weigh_sent_recipients_reads_the_sent_folders_recent_mail_first) {
    LocalStore s = FreshStore("sentweight");
    AddAccountWithInbox(s, "erika", "erika@example.com", "Erika");
    Folder sent; sent.accountId = "erika"; sent.name = "Sent"; sent.role = FolderRole::Sent;
    REQUIRE(s.UpsertFolder(sent).success);
    const int64_t now = 1800000000;
    const int64_t day = 86400;
    auto sentTo = [&](int64_t uid, int64_t date, const std::vector<std::string>& to,
                      uint32_t flags = 0) {
        MessageEnvelope m = Incoming("erika", uid, "erika@example.com", to);
        m.folder = "Sent";
        m.date = date;
        m.flags = flags;
        REQUIRE(s.UpsertMessage(m).success);
    };
    sentTo(1, now, {"Anna Schmidt <Anna@Example.com>", "max@example.com"});
    sentTo(2, now - 90 * day, {"anna@example.com", "anna@example.com"});   // listed twice: once
    sentTo(3, now, {"max@example.com"}, Flag_Deleted);                     // deleted: left out
    // Four messages a year ago weigh less than one from today.
    for (int64_t uid = 4; uid < 8; ++uid) sentTo(uid, now - 360 * day, {"old@example.com"});
    sentTo(8, now + 5 * day, {"future@example.com"});   // a wrong clock: weighs as now
    // Mail received is not mail written.
    REQUIRE(s.UpsertMessage(Incoming("erika", 9, "carol@acme.com", {"erika@example.com"})).success);

    std::map<std::string, double> w;
    REQUIRE(s.WeighSentRecipients(w, now).success);
    REQUIRE(w.size() == 4);
    auto near = [](double a, double b) { return a > b - 1e-9 && a < b + 1e-9; };
    REQUIRE(near(w["anna@example.com"], 1.5));     // today 1 + 90 days 1/2
    REQUIRE(near(w["max@example.com"], 1.0));
    REQUIRE(near(w["old@example.com"], 0.25));     // 4 x 1/16
    REQUIRE(near(w["future@example.com"], 1.0));
    REQUIRE(w["old@example.com"] < w["max@example.com"]);
}
