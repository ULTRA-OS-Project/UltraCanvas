// Tests/UltraMail/test_localstore.cpp
// Exercises the UltraMail LocalStore against an in-memory UltraDatabase:
// schema/migrations, accounts, folders, message upserts, the needs-answer
// eligibility rules, flag updates, and the per-account status rollup that
// drives the info-tile bar.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "UltraMailLocalStore.h"

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

TEST(unread_counts_inbox_unseen) {
    LocalStore s = FreshStore("unread");
    AddAccountWithInbox(s, "erika", "erika@example.com", "erika");
    REQUIRE(s.UpsertMessage(Incoming("erika", 1, "a@x.com", {"erika@example.com"})).success);
    REQUIRE(s.UpsertMessage(Incoming("erika", 2, "b@x.com", {"erika@example.com"})).success);
    REQUIRE_EQ(UnreadFor(s, "erika"), 2);

    REQUIRE(s.SetFlags("erika", "INBOX", 1, Flag_Seen, true).success);
    REQUIRE_EQ(UnreadFor(s, "erika"), 1);
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
