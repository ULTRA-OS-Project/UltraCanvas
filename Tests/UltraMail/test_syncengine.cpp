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
    int fetchMessageCalls = 0;          // per-message fetches (slow path)
    int fetchBodiesCalls  = 0;          // batched fetches (fast path)
    std::vector<uint32_t> lastBodyUids; // UIDs the batch was asked for

    UltraNetResult FetchMessage(const std::string&, const std::string& folder, uint32_t uid,
                                std::string& outRaw, const UltraNetMailOptions&) override {
        ++fetchMessageCalls;
        auto it = bodies.find(folder + "/" + std::to_string(uid));
        if (it == bodies.end())
            return UltraNetResult::Error(UltraNetResultCode::NotFound, "no body");
        outRaw = it->second;
        return UltraNetResult::Ok();
    }
    // Record the batch entry point, then delegate to the base default so the
    // bodies still stream through (the real IMAP plug-in reuses one connection).
    UltraNetResult FetchMessageBodies(
        const std::string& serverUrl, const std::string& folder,
        const std::vector<uint32_t>& uids,
        const std::function<void(uint32_t, const std::string&)>& onMessage,
        const UltraNetMailOptions& options) override {
        ++fetchBodiesCalls;
        lastBodyUids = uids;
        return IMailboxProtocolPlugin::FetchMessageBodies(serverUrl, folder, uids, onMessage, options);
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
    // Server-side (uid -> flags) per folder the reconcile reads; the presence of
    // a uid here is the "live" set (drives deletion detection), and its flags are
    // reported flagsKnown=true. When `fetchAllFlagsFails` is set the call errors
    // so the reconcile must skip its expunge pass rather than treat "no data" as
    // "the folder is empty"; a folder with an empty list is a successful but empty
    // enumeration (also must not expunge while locals exist).
    std::map<std::string, std::vector<std::pair<uint32_t, UltraNetMailFlags>>> serverFlags;
    bool fetchAllFlagsFails = false;
    UltraNetResult FetchAllFlags(
        const std::string&, const std::string& folder,
        const std::function<void(uint32_t, UltraNetMailFlags, bool)>& onFlags,
        const UltraNetMailOptions&) override {
        if (fetchAllFlagsFails)
            return UltraNetResult::Error(UltraNetResultCode::ReceiveFailed, "boom");
        auto it = serverFlags.find(folder);
        if (it != serverFlags.end())
            for (const auto& pr : it->second) onFlags(pr.first, pr.second, /*flagsKnown=*/true);
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

TEST(sync_messages_streams_each_header) {
    Fixture fx("stream");
    SyncEngine engine(fx.store, fx.fake, fx.emlDir);
    UltraNetMailOptions opts;
    engine.SyncFolders("erika", "imaps://x/", opts);

    // onMessageStored fires once per header as it lands, so the UI can fill the
    // list incrementally instead of waiting for the whole mailbox.
    std::vector<MessageEnvelope> streamed;
    SyncOutcome r = engine.SyncMessages(
        "erika", "INBOX", "imaps://x/", opts, /*fetchBodies=*/false,
        [&](const MessageEnvelope& m) { streamed.push_back(m); });
    REQUIRE(r.ok);

    // One callback per envelope, and the count matches what was stored.
    REQUIRE_EQ(streamed.size(), (size_t)3);
    REQUIRE_EQ(r.stats.messages, 3);

    // The streamed value is the decoded envelope (name/address parsed), delivered
    // in the plug-in's order (uids 1, 2, 3 here) — not a bare uid.
    REQUIRE(streamed[0].uid == 1 && streamed[1].uid == 2 && streamed[2].uid == 3);
    REQUIRE_EQ(streamed[0].fromAddr, std::string("boss@acme.com"));
    REQUIRE_EQ(streamed[0].fromName, std::string("Boss"));

    // Each message was already persisted by the time its callback ran.
    std::vector<MessageEnvelope> msgs;
    fx.store.ListMessages("erika", "INBOX", 0, msgs);
    REQUIRE_EQ(msgs.size(), (size_t)3);
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

    // The engine must fetch all bodies through the ONE batched entry point (which
    // the IMAP plug-in serves over a single reused connection), not reconnect per
    // message. One call, carrying every UID.
    REQUIRE_EQ(fx.fake.fetchBodiesCalls, 1);
    REQUIRE_EQ(fx.fake.lastBodyUids.size(), static_cast<std::size_t>(3));

    const std::string path = engine.BodyPath("erika", "INBOX", 1);
    REQUIRE(fs::exists(path));
    std::ifstream is(path, std::ios::binary);
    std::string raw((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());
    ParsedMessage pm = MimeCodec::Parse(raw);
    REQUIRE_EQ(pm.subject, std::string("Please reply"));
    REQUIRE(pm.body.find("Can you reply soon?") != std::string::npos);
}

TEST(a_downloaded_body_is_scanned_once_and_its_verdict_stored) {
    Fixture fx("scan");
    // A phishing body under UID 1: the link says paypal.com and goes to a
    // numeric address. The scan runs where the body is cached, so the message
    // list can colour its badge without ever re-reading the .eml.
    fx.fake.bodies["INBOX/1"] = BuildRaw(
        "Boss <boss@acme.com>", "Please reply",
        "<html><body><a href=\"http://198.51.100.7/login\">www.paypal.com</a>"
        "</body></html>");

    SyncEngine engine(fx.store, fx.fake, fx.emlDir);
    UltraNetMailOptions opts;
    engine.SyncFolders("erika", "imaps://x/", opts);
    REQUIRE(engine.SyncMessages("erika", "INBOX", "imaps://x/", opts,
                                /*fetchBodies=*/true).ok);

    MessageSecurity sec;
    REQUIRE(fx.store.GetSecurity("erika", "INBOX", 1, sec).success);
    REQUIRE(sec.Scanned());
    REQUIRE(sec.level == ThreatLevel::Scam);
    REQUIRE(sec.reason.find("198.51.100.7") != std::string::npos);

    // An ordinary message in the same batch is scanned too, and comes out clean.
    MessageSecurity ordinary;
    REQUIRE(fx.store.GetSecurity("erika", "INBOX", 2, ordinary).success);
    REQUIRE(ordinary.Scanned());
    REQUIRE(ordinary.level == ThreatLevel::Clean);
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

// Reading a message's stored flags by uid (0 when the row is gone).
static uint32_t FlagsFor(LocalStore& s, int64_t uid) {
    std::vector<MessageEnvelope> msgs;
    s.ListMessages("erika", "INBOX", 0, msgs);
    for (const auto& m : msgs) if (m.uid == uid) return m.flags;
    return 0;
}
static bool HasUid(LocalStore& s, int64_t uid) {
    std::vector<MessageEnvelope> msgs;
    s.ListMessages("erika", "INBOX", 0, msgs);
    for (const auto& m : msgs) if (m.uid == uid) return true;
    return false;
}

TEST(reconcile_flags_updates_read_state_and_expunges) {
    Fixture fx("reconcile");
    SyncEngine engine(fx.store, fx.fake, fx.emlDir);
    UltraNetMailOptions opts;
    engine.SyncFolders("erika", "imaps://x/", opts);
    engine.SyncMessages("erika", "INBOX", "imaps://x/", opts);
    // Seeded: uid1 unread, uid2 answered, uid3 seen — all three present.
    REQUIRE(!(FlagsFor(fx.store, 1) & Flag_Seen));
    REQUIRE(HasUid(fx.store, 3));
    REQUIRE_EQ(UnreadFor(fx.store), 2);       // uid1 + uid2 unseen

    // Another client read uid1 and deleted uid3; uid2 is unchanged.
    fx.fake.serverFlags["INBOX"] = {
        { 1u, UltraNetMailFlags::Seen },
        { 2u, UltraNetMailFlags::Answered },
    };
    SyncOutcome r = engine.ReconcileFlags("erika", "INBOX", "imaps://x/", opts);
    REQUIRE(r.ok);
    REQUIRE_EQ(r.stats.reconciled, 1);        // uid1 flipped to Seen
    REQUIRE_EQ(r.stats.expunged, 1);          // uid3 removed
    REQUIRE(FlagsFor(fx.store, 1) & Flag_Seen);
    REQUIRE(!HasUid(fx.store, 3));
    // uid1 is now read and uid3 is gone; uid2 (answered but never seen) is the
    // one message still unread.
    REQUIRE_EQ(UnreadFor(fx.store), 1);
}

TEST(reconcile_flags_never_expunges_when_the_server_fetch_fails) {
    Fixture fx("reconcile-fail");
    SyncEngine engine(fx.store, fx.fake, fx.emlDir);
    UltraNetMailOptions opts;
    engine.SyncFolders("erika", "imaps://x/", opts);
    engine.SyncMessages("erika", "INBOX", "imaps://x/", opts);

    // A failed flag fetch must be a no-op, not "the folder is empty" — otherwise
    // a transient network error would delete every locally-held message.
    fx.fake.fetchAllFlagsFails = true;
    SyncOutcome r = engine.ReconcileFlags("erika", "INBOX", "imaps://x/", opts);
    REQUIRE(r.ok);                            // non-fatal
    REQUIRE_EQ(r.stats.expunged, 0);
    REQUIRE(HasUid(fx.store, 1));
    REQUIRE(HasUid(fx.store, 2));
    REQUIRE(HasUid(fx.store, 3));
}

TEST(reconcile_flags_never_expunges_on_empty_enumeration) {
    Fixture fx("reconcile-empty");
    SyncEngine engine(fx.store, fx.fake, fx.emlDir);
    UltraNetMailOptions opts;
    engine.SyncFolders("erika", "imaps://x/", opts);
    engine.SyncMessages("erika", "INBOX", "imaps://x/", opts);

    // The regression that wiped the cache: a SUCCESSFUL fetch that reports no
    // UIDs (an empty/uncaptured server response) must not be read as "the folder
    // is empty" while we still hold messages — it must delete nothing.
    fx.fake.serverFlags["INBOX"] = {};        // success, but enumerates nothing
    SyncOutcome r = engine.ReconcileFlags("erika", "INBOX", "imaps://x/", opts);
    REQUIRE(r.ok);
    REQUIRE_EQ(r.stats.expunged, 0);
    REQUIRE(HasUid(fx.store, 1));
    REQUIRE(HasUid(fx.store, 2));
    REQUIRE(HasUid(fx.store, 3));
}
