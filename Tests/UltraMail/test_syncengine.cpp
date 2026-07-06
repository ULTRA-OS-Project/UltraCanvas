// Tests/UltraMail/test_syncengine.cpp
// Drives the SyncEngine against a fake IMailboxProtocolPlugin (canned folders /
// envelopes / bodies) — no live server — and checks it populates the LocalStore
// correctly: folder sync, envelope sync with needs-answer computation,
// incremental fetch, raw-body caching (parseable by MimeCodec) and two-sided
// flag changes.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "UltraMailSyncEngine.h"
#include "UltraMailLocalStore.h"
#include "UltraMailMimeCodec.h"

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetPlugins.h>
#include <UltraNet/UltraNetMime.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace UltraMail;

namespace {

// A canned mailbox for tests. Implements the full IMailboxProtocolPlugin
// surface but serves in-memory data and records flag changes.
class FakeMailbox : public IMailboxProtocolPlugin {
public:
    std::vector<UltraNetMailFolder> folders;
    std::map<std::string, std::vector<UltraNetMailEnvelope>> envelopes;  // folder -> envs
    std::map<std::string, std::string> bodies;                          // "folder/uid" -> raw

    struct FlagCall { std::string folder; uint32_t uid; UltraNetMailFlags flags; bool set; };
    std::vector<FlagCall> flagCalls;

    // IUltraNetPlugin
    std::string GetName() const override { return "FakeMailbox"; }
    std::string GetVersion() const override { return "0.0.1"; }
    std::vector<std::string> GetSupportedSchemes() const override { return {"imap", "imaps"}; }
    UltraNetResult Initialize(const UltraNetConfig&) override { return UltraNetResult::Ok(); }
    void Shutdown() override {}

    // IMailProtocolPlugin
    UltraNetResult SendMail(const UltraNetMailMessage&, const UltraNetMailOptions&) override {
        return UltraNetResult::Error(UltraNetResultCode::UnsupportedScheme, "fake");
    }
    UltraNetResult FetchMessages(const std::string&, std::vector<UltraNetMailMessage>&,
                                 const UltraNetMailOptions&) override {
        return UltraNetResult::Ok();
    }

    // IMailboxProtocolPlugin
    UltraNetResult ListFolders(const std::string&, std::vector<UltraNetMailFolder>& out,
                               const UltraNetMailOptions&) override {
        out = folders;
        return UltraNetResult::Ok();
    }
    UltraNetResult GetMailboxStatus(const std::string&, const std::string&,
                                    UltraNetMailboxStatus&, const UltraNetMailOptions&) override {
        return UltraNetResult::Ok();
    }
    UltraNetResult FetchEnvelopes(const std::string&, const std::string& folder,
                                  uint32_t sinceUid, std::vector<UltraNetMailEnvelope>& out,
                                  const UltraNetMailOptions&) override {
        out.clear();
        auto it = envelopes.find(folder);
        if (it != envelopes.end())
            for (const auto& e : it->second)
                if (e.uid > sinceUid) out.push_back(e);
        return UltraNetResult::Ok();
    }
    UltraNetResult FetchMessage(const std::string&, const std::string& folder, uint32_t uid,
                                std::string& outRaw, const UltraNetMailOptions&) override {
        auto it = bodies.find(folder + "/" + std::to_string(uid));
        if (it == bodies.end())
            return UltraNetResult::Error(UltraNetResultCode::NotFound, "no body");
        outRaw = it->second;
        return UltraNetResult::Ok();
    }
    UltraNetResult StoreFlags(const std::string&, const std::string& folder, uint32_t uid,
                              UltraNetMailFlags flags, bool set, const UltraNetMailOptions&) override {
        flagCalls.push_back({folder, uid, flags, set});
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

UltraNetMailFolder MakeFolder(const std::string& name, const std::string& role) {
    UltraNetMailFolder f; f.name = name; f.role = role; f.delimiter = "/"; return f;
}

UltraNetMailEnvelope Env(uint32_t uid, const std::string& from,
                         const std::vector<std::string>& to, const std::string& subject,
                         UltraNetMailFlags flags) {
    UltraNetMailEnvelope e;
    e.uid = uid; e.from = from; e.to = to; e.subject = subject; e.flags = flags;
    e.date = "Wed, 14 Jan 2026 10:0" + std::to_string(uid % 10) + ":00 +0000";
    e.messageId = "<" + std::to_string(uid) + "@x>";
    return e;
}

std::string BuildRaw(const std::string& from, const std::string& subject,
                     const std::string& body) {
    UltraNetMimeBuildInput in;
    in.from = from; in.to = {"erika@example.com"}; in.subject = subject; in.body = body;
    in.date = "Wed, 14 Jan 2026 10:00:00 +0000"; in.messageId = "<b@x>";
    return UltraNet_MimeBuild(in);
}

// A store + account + fake wired for INBOX with three seeded messages.
struct Fixture {
    LocalStore store;
    FakeMailbox fake;
    std::string emlDir;

    explicit Fixture(const std::string& tag) {
        REQUIRE(store.Open("sync-" + tag, ":memory:").success);
        Account a; a.accountId = "erika"; a.email = "erika@example.com"; a.shortName = "erika";
        REQUIRE(store.UpsertAccount(a).success);

        emlDir = (fs::temp_directory_path() / ("ultramail_sync_" + tag)).string();
        fs::remove_all(emlDir);

        fake.folders = { MakeFolder("INBOX", "inbox"), MakeFolder("Sent", "sent") };
        fake.envelopes["INBOX"] = {
            Env(1, "Boss <boss@acme.com>",  {"erika@example.com"}, "Please reply", UltraNetMailFlags::None),
            Env(2, "Ann <ann@x.com>",       {"erika@example.com"}, "Re: thanks",   UltraNetMailFlags::Answered),
            Env(3, "List <list@x.com>",     {"other@x.com"},       "Newsletter",   UltraNetMailFlags::Seen),
        };
        fake.bodies["INBOX/1"] = BuildRaw("Boss <boss@acme.com>", "Please reply", "Can you reply soon?");
        fake.bodies["INBOX/2"] = BuildRaw("Ann <ann@x.com>", "Re: thanks", "thanks!");
        fake.bodies["INBOX/3"] = BuildRaw("List <list@x.com>", "Newsletter", "news");
    }
    ~Fixture() { std::error_code ec; fs::remove_all(emlDir, ec); }
};

int NeedsFor(LocalStore& s) {
    std::vector<AccountStatus> st;
    s.GetAccountStatus(st);
    for (auto& x : st) if (x.accountId == "erika") return x.needsAnswer;
    return -1;
}
int UnreadFor(LocalStore& s) {
    std::vector<AccountStatus> st;
    s.GetAccountStatus(st);
    for (auto& x : st) if (x.accountId == "erika") return x.unread;
    return -1;
}

} // namespace

TEST(sync_folders_populates_store) {
    Fixture fx("folders");
    SyncEngine engine(fx.store, fx.fake, fx.emlDir);
    UltraNetMailOptions opts;

    SyncOutcome r = engine.SyncFolders("erika", "imaps://mail.example.com/", opts);
    REQUIRE(r.ok);
    REQUIRE_EQ(r.stats.folders, 2);

    std::vector<Folder> folders;
    REQUIRE(fx.store.ListFolders("erika", folders).success);
    REQUIRE_EQ(folders.size(), (size_t)2);
    bool inbox = false;
    for (auto& f : folders) if (f.name == "INBOX") { inbox = true; REQUIRE(f.role == FolderRole::Inbox); }
    REQUIRE(inbox);
}

TEST(sync_messages_computes_needs_answer) {
    Fixture fx("msgs");
    SyncEngine engine(fx.store, fx.fake, fx.emlDir);
    UltraNetMailOptions opts;
    engine.SyncFolders("erika", "imaps://x/", opts);

    SyncOutcome r = engine.SyncMessages("erika", "INBOX", "imaps://x/", opts);
    REQUIRE(r.ok);
    REQUIRE_EQ(r.stats.messages, 3);

    std::vector<MessageEnvelope> msgs;
    fx.store.ListMessages("erika", "INBOX", 0, msgs);
    REQUIRE_EQ(msgs.size(), (size_t)3);
    // From "Boss <boss@acme.com>" is parsed into name + address.
    bool found = false;
    for (auto& m : msgs) if (m.uid == 1) {
        found = true;
        REQUIRE_EQ(m.fromAddr, std::string("boss@acme.com"));
        REQUIRE_EQ(m.fromName, std::string("Boss"));
    }
    REQUIRE(found);

    // uid 1: to owner, unanswered -> needs answer; uid 2: answered; uid 3: not to owner.
    REQUIRE_EQ(NeedsFor(fx.store), 1);
    // unread inbox unseen: uid1 (unseen) + uid2 (unseen) = 2; uid3 is Seen.
    REQUIRE_EQ(UnreadFor(fx.store), 2);
}

TEST(sync_messages_is_incremental) {
    Fixture fx("incr");
    SyncEngine engine(fx.store, fx.fake, fx.emlDir);
    UltraNetMailOptions opts;
    engine.SyncFolders("erika", "imaps://x/", opts);
    engine.SyncMessages("erika", "INBOX", "imaps://x/", opts);

    // A new message arrives.
    fx.fake.envelopes["INBOX"].push_back(
        Env(4, "New <new@x.com>", {"erika@example.com"}, "Question", UltraNetMailFlags::None));

    SyncOutcome r = engine.SyncMessages("erika", "INBOX", "imaps://x/", opts);
    REQUIRE(r.ok);
    REQUIRE_EQ(r.stats.messages, 1);   // only the new one (sinceUid was 3)

    std::vector<MessageEnvelope> msgs;
    fx.store.ListMessages("erika", "INBOX", 0, msgs);
    REQUIRE_EQ(msgs.size(), (size_t)4);
}

TEST(fetch_bodies_writes_parseable_eml) {
    Fixture fx("bodies");
    SyncEngine engine(fx.store, fx.fake, fx.emlDir);
    UltraNetMailOptions opts;
    engine.SyncFolders("erika", "imaps://x/", opts);

    SyncOutcome r = engine.SyncMessages("erika", "INBOX", "imaps://x/", opts, /*fetchBodies=*/true);
    REQUIRE(r.ok);
    REQUIRE_EQ(r.stats.bodies, 3);

    const std::string path = engine.BodyPath("erika", "INBOX", 1);
    REQUIRE(fs::exists(path));
    std::ifstream is(path, std::ios::binary);
    std::string raw((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());
    ParsedMessage pm = MimeCodec::Parse(raw);
    REQUIRE_EQ(pm.subject, std::string("Please reply"));
    REQUIRE(pm.body.find("Can you reply soon?") != std::string::npos);
}

TEST(set_flag_updates_server_and_local) {
    Fixture fx("flags");
    SyncEngine engine(fx.store, fx.fake, fx.emlDir);
    UltraNetMailOptions opts;
    engine.SyncFolders("erika", "imaps://x/", opts);
    engine.SyncMessages("erika", "INBOX", "imaps://x/", opts);
    REQUIRE_EQ(NeedsFor(fx.store), 1);        // uid 1 pending

    // Answering uid 1 clears its needs-answer, both server-side and locally.
    SyncOutcome r = engine.SetFlag("erika", "INBOX", 1, Flag_Answered, true, "imaps://x/", opts);
    REQUIRE(r.ok);
    REQUIRE_EQ(fx.fake.flagCalls.size(), (size_t)1);
    REQUIRE(fx.fake.flagCalls[0].uid == 1u);
    REQUIRE(fx.fake.flagCalls[0].set == true);
    REQUIRE(UltraNetHasFlag(fx.fake.flagCalls[0].flags, UltraNetMailFlags::Answered));

    REQUIRE_EQ(NeedsFor(fx.store), 0);
}
