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
#include "UltraMailSyncService.h"
#include "UltraMailLocalStore.h"
#include "UltraMailMimeCodec.h"

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetPlugins.h>
#include <UltraNet/UltraNetMime.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>
#include "../../UltraCanvas/include/UltraCanvasPathUtf8.h"

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
    bool listFoldersFails = false;
    UltraNetResult ListFolders(const std::string&, std::vector<UltraNetMailFolder>& out,
                               const UltraNetMailOptions&) override {
        if (listFoldersFails)
            return UltraNetResult::Error(UltraNetResultCode::ReceiveFailed, "list failed");
        out = folders;
        return UltraNetResult::Ok();
    }
    uint32_t uidValidity = 0;   // 0 = "STATUS unsupported" (skips the renumber check)
    uint32_t uidNext = 0;       // 0 = not reported (skips the "cache ahead" check)
    UltraNetResult GetMailboxStatus(const std::string&, const std::string& folder,
                                    UltraNetMailboxStatus& out, const UltraNetMailOptions&) override {
        out = UltraNetMailboxStatus{};
        out.uidValidity = uidValidity;
        out.uidNext = uidNext;
        auto it = envelopes.find(folder);
        out.messages = it == envelopes.end() ? 0 : static_cast<uint32_t>(it->second.size());
        return UltraNetResult::Ok();
    }
    // A real server answers "UID SEARCH UID n:*" with the highest UID even when
    // it is below n (RFC 3501): set to serve that echo.
    bool echoHighest = false;
    int  fetchEnvelopesCalls = 0;
    // Set to fail every envelope fetch with this UltraNet code — the way the
    // IMAP plug-in fails when the server cannot be reached (HostNotFound) or
    // refuses the sign-in (AuthenticationFailed).
    UltraNetResultCode fetchEnvelopesFail = UltraNetResultCode::Success;
    UltraNetResult FetchEnvelopes(const std::string&, const std::string& folder,
                                  uint32_t sinceUid, std::vector<UltraNetMailEnvelope>& out,
                                  const UltraNetMailOptions&) override {
        out.clear();
        ++fetchEnvelopesCalls;
        if (fetchEnvelopesFail != UltraNetResultCode::Success) {
            UltraNetResult r = UltraNetResult::Error(fetchEnvelopesFail, "could not fetch");
            r.diagnostics = "Server: imaps://x/";
            return r;
        }
        auto it = envelopes.find(folder);
        if (it != envelopes.end())
            for (const auto& e : it->second)
                if (e.uid > sinceUid) out.push_back(e);
        if (echoHighest && out.empty() && sinceUid > 0 && it != envelopes.end() &&
            !it->second.empty()) {
            const UltraNetMailEnvelope* top = &it->second.front();
            for (const auto& e : it->second) if (e.uid > top->uid) top = &e;
            out.push_back(*top);
        }
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
        fs::remove_all(UltraCanvas::PathFromUtf8(emlDir));

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
    ~Fixture() { std::error_code ec; fs::remove_all(UltraCanvas::PathFromUtf8(emlDir), ec); }
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

TEST(sync_outcome_classifies_a_network_gap) {
    // The codes UltraNet produces when the server is not there at all …
    for (auto c : {UltraNetResultCode::HostNotFound, UltraNetResultCode::ConnectionRefused,
                   UltraNetResultCode::ConnectionReset, UltraNetResultCode::ConnectionTimeout,
                   UltraNetResultCode::Timeout}) {
        SyncOutcome o = SyncOutcome::Fail(UltraNetResult::Error(c, "gap"));
        REQUIRE(!o.ok);
        REQUIRE(o.code == c);
        REQUIRE(o.NetworkUnreachable());
    }
    // … and the ones that prove it answered.
    for (auto c : {UltraNetResultCode::AuthenticationFailed, UltraNetResultCode::TlsCertificateInvalid,
                   UltraNetResultCode::TlsHandshakeFailed, UltraNetResultCode::AccessDenied,
                   UltraNetResultCode::NotFound, UltraNetResultCode::Unknown}) {
        SyncOutcome o = SyncOutcome::Fail(UltraNetResult::Error(c, "no"));
        REQUIRE(!o.NetworkUnreachable());
    }
    // A failure that did not come from UltraNet carries no code.
    SyncOutcome local = SyncOutcome::Fail("disk full");
    REQUIRE(local.code == UltraNetResultCode::Unknown);
    REQUIRE(!local.NetworkUnreachable());
    // Success is never a gap.
    REQUIRE(!SyncOutcome{}.NetworkUnreachable());
}

TEST(sync_now_keeps_the_inbox_failure_code_and_details) {
    Fixture fx("syncnow-fail");
    SyncService svc(fx.store, fx.fake, fx.emlDir);
    UltraNetMailOptions opts;

    fx.fake.fetchEnvelopesFail = UltraNetResultCode::HostNotFound;
    SyncOutcome r = svc.SyncNow("erika", "imaps://x/", opts);
    REQUIRE(!r.ok);
    REQUIRE(r.code == UltraNetResultCode::HostNotFound);
    REQUIRE(r.NetworkUnreachable());
    REQUIRE_EQ(r.message, std::string("could not fetch"));
    REQUIRE_EQ(r.diagnostics, std::string("Server: imaps://x/"));
    REQUIRE_EQ(r.stats.folders, 2);           // the folder LIST before it still counts

    fx.fake.fetchEnvelopesFail = UltraNetResultCode::AuthenticationFailed;
    r = svc.SyncNow("erika", "imaps://x/", opts);
    REQUIRE(!r.ok);
    REQUIRE(!r.NetworkUnreachable());

    fx.fake.fetchEnvelopesFail = UltraNetResultCode::Success;
    r = svc.SyncNow("erika", "imaps://x/", opts);
    REQUIRE(r.ok);
    REQUIRE(r.code == UltraNetResultCode::Success);
}

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

TEST(cached_body_path_keeps_a_non_ascii_folder_name) {
    // IMAP folder names are often outside the ANSI code page ("Entw\xc3\xbcrfe",
    // "\xd0\x9a\xd0\xbe\xd1\x80\xd0\xb7\xd0\xb8\xd0\xbd\xd0\xb0"). Joined onto the path as a
    // bare std::string, Windows converted them in that code page and the body
    // was cached under a mangled folder - one EmailCleaner, reading the same
    // cache as UTF-8, never found. Every part must go through PathFromUtf8.
    for (const std::string folder : { std::string("Entw\xc3\xbcrfe"),
                                      std::string("\xd0\x9a\xd0\xbe\xd1\x80\xd0\xb7\xd0\xb8\xd0\xbd\xd0\xb0") }) {
        const std::string expected = UltraCanvas::PathToUtf8(
            UltraCanvas::PathFromUtf8("cache") / UltraCanvas::PathFromUtf8("erika-\xc3\xb6") /
            UltraCanvas::PathFromUtf8(folder) / "7.eml");
        REQUIRE_EQ(CachedBodyPath("cache", "erika-\xc3\xb6", folder, 7), expected);
    }
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
    REQUIRE(fs::exists(UltraCanvas::PathFromUtf8(path)));
    std::ifstream is(UltraCanvas::PathFromUtf8(path), std::ios::binary);
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

TEST(sync_messages_resets_the_folder_when_uidvalidity_changes) {
    Fixture fx("uidvalidity");
    SyncEngine engine(fx.store, fx.fake, fx.emlDir);
    UltraNetMailOptions opts;
    engine.SyncFolders("erika", "imaps://x/", opts);

    // First sync under UIDVALIDITY 100: the seeded uid1/2/3 land and the folder
    // records validity 100.
    fx.fake.uidValidity = 100;
    engine.SyncMessages("erika", "INBOX", "imaps://x/", opts);
    REQUIRE(HasUid(fx.store, 1));
    REQUIRE(HasUid(fx.store, 3));

    // The server renumbers the mailbox (new UIDVALIDITY) and the same messages
    // come back under fresh, LOWER uids — an incremental fetch keyed on the old
    // max uid would miss them. The engine must drop the stale cache and refetch.
    fx.fake.uidValidity = 200;
    fx.fake.envelopes["INBOX"] = {
        Env(10, "Boss <boss@acme.com>", {"erika@example.com"}, "Please reply", UltraNetMailFlags::None),
        Env(11, "Ann <ann@x.com>",      {"erika@example.com"}, "Re: thanks",   UltraNetMailFlags::Seen),
    };
    engine.SyncMessages("erika", "INBOX", "imaps://x/", opts);
    REQUIRE(!HasUid(fx.store, 1));            // stale uids discarded
    REQUIRE(!HasUid(fx.store, 3));
    REQUIRE(HasUid(fx.store, 10));            // refetched from UID 0
    REQUIRE(HasUid(fx.store, 11));
}

// ---- The body cache follows the index ---------------------------------------

namespace {
bool BodyCached(const SyncEngine& engine, int64_t uid, const std::string& folder = "INBOX") {
    return fs::exists(engine.BodyPath("erika", folder, uid));
}
} // namespace

TEST(reconcile_flags_deletes_the_bodies_of_expunged_mail) {
    Fixture fx("reconcile-bodies");
    SyncEngine engine(fx.store, fx.fake, fx.emlDir);
    UltraNetMailOptions opts;
    engine.SyncFolders("erika", "imaps://x/", opts);
    engine.SyncMessages("erika", "INBOX", "imaps://x/", opts, /*fetchBodies=*/true);
    REQUIRE(BodyCached(engine, 1));
    REQUIRE(BodyCached(engine, 2));
    REQUIRE(BodyCached(engine, 3));

    // An earlier version dropped uid2's row when the server expunged it but
    // left the body behind; and a sync running alongside has just written a
    // newer message's body (uid 9, not in the index snapshot yet).
    REQUIRE(fx.store.RemoveMessage("erika", "INBOX", 2).success);
    REQUIRE(!engine.WriteBody("erika", "INBOX", 9, fx.fake.bodies["INBOX/1"]).empty());

    // The server now lists uid1 only: uid3 was deleted elsewhere.
    fx.fake.serverFlags["INBOX"] = { { 1u, UltraNetMailFlags::Seen } };
    SyncOutcome r = engine.ReconcileFlags("erika", "INBOX", "imaps://x/", opts);
    REQUIRE(r.ok);
    REQUIRE_EQ(r.stats.expunged, 1);         // uid3's row
    REQUIRE_EQ(r.stats.bodiesRemoved, 2);    // uid3's body + uid2's leftover
    REQUIRE(BodyCached(engine, 1));
    REQUIRE(!BodyCached(engine, 2));
    REQUIRE(!BodyCached(engine, 3));
    REQUIRE(BodyCached(engine, 9));          // newer than the snapshot: untouched
}

TEST(reconcile_flags_keeps_every_body_when_the_folder_was_not_enumerated) {
    Fixture fx("reconcile-bodies-fail");
    SyncEngine engine(fx.store, fx.fake, fx.emlDir);
    UltraNetMailOptions opts;
    engine.SyncFolders("erika", "imaps://x/", opts);
    engine.SyncMessages("erika", "INBOX", "imaps://x/", opts, /*fetchBodies=*/true);

    fx.fake.serverFlags["INBOX"] = {};       // success, but enumerates nothing
    SyncOutcome r = engine.ReconcileFlags("erika", "INBOX", "imaps://x/", opts);
    REQUIRE(r.ok);
    REQUIRE_EQ(r.stats.bodiesRemoved, 0);
    REQUIRE(BodyCached(engine, 1));
    REQUIRE(BodyCached(engine, 2));
    REQUIRE(BodyCached(engine, 3));
}

TEST(move_message_deletes_the_source_body) {
    Fixture fx("move-body");
    SyncEngine engine(fx.store, fx.fake, fx.emlDir);
    UltraNetMailOptions opts;
    engine.SyncFolders("erika", "imaps://x/", opts);
    engine.SyncMessages("erika", "INBOX", "imaps://x/", opts, /*fetchBodies=*/true);
    REQUIRE(BodyCached(engine, 2));

    REQUIRE(engine.MoveMessage("erika", "INBOX", 2, "Trash", "imaps://x/", opts).ok);
    REQUIRE(!HasUid(fx.store, 2));
    REQUIRE(!BodyCached(engine, 2));
    REQUIRE(BodyCached(engine, 1));
}

TEST(forget_message_drops_the_row_and_the_body) {
    Fixture fx("forget");
    SyncEngine engine(fx.store, fx.fake, fx.emlDir);
    UltraNetMailOptions opts;
    engine.SyncFolders("erika", "imaps://x/", opts);
    engine.SyncMessages("erika", "INBOX", "imaps://x/", opts, /*fetchBodies=*/true);

    REQUIRE(engine.ForgetMessage("erika", "INBOX", 3).success);
    REQUIRE(!HasUid(fx.store, 3));
    REQUIRE(!BodyCached(engine, 3));
    // A message whose body was never downloaded is forgotten just the same.
    REQUIRE(fx.store.RemoveMessage("erika", "INBOX", 1).success);
    REQUIRE(engine.ForgetMessage("erika", "INBOX", 1).success);
}

TEST(a_uidvalidity_reset_deletes_the_folders_old_bodies) {
    Fixture fx("uidvalidity-bodies");
    SyncEngine engine(fx.store, fx.fake, fx.emlDir);
    UltraNetMailOptions opts;
    engine.SyncFolders("erika", "imaps://x/", opts);
    fx.fake.uidValidity = 100;
    engine.SyncMessages("erika", "INBOX", "imaps://x/", opts, /*fetchBodies=*/true);
    REQUIRE(BodyCached(engine, 1));
    REQUIRE(BodyCached(engine, 3));

    fx.fake.uidValidity = 200;
    fx.fake.envelopes["INBOX"] = {
        Env(10, "Boss <boss@acme.com>", {"erika@example.com"}, "Please reply", UltraNetMailFlags::None),
    };
    fx.fake.bodies["INBOX/10"] = fx.fake.bodies["INBOX/1"];
    engine.SyncMessages("erika", "INBOX", "imaps://x/", opts, /*fetchBodies=*/true);
    REQUIRE(!BodyCached(engine, 1));         // the old numbering's files are gone
    REQUIRE(!BodyCached(engine, 3));
    REQUIRE(BodyCached(engine, 10));         // the new one is cached
}

// ---- Mail an incremental fetch cannot reach --------------------------------
// "UID > the highest held" never looks below the highest UID again. These
// cover the ways mail ended up there and stayed out of the list for good.

namespace {
std::string SubjectOf(LocalStore& s, int64_t uid) {
    std::vector<MessageEnvelope> msgs;
    s.ListMessages("erika", "INBOX", 0, msgs);
    for (const auto& m : msgs) if (m.uid == uid) return m.subject;
    return "<none>";
}
// The server lists exactly the fake's INBOX envelopes, with their flags.
void ServerListsInbox(FakeMailbox& fake) {
    fake.serverFlags["INBOX"].clear();
    for (const auto& e : fake.envelopes["INBOX"])
        fake.serverFlags["INBOX"].push_back({e.uid, e.flags});
}
} // namespace

TEST(refresh_folder_fetches_mail_an_earlier_sync_skipped) {
    Fixture fx("refresh-missed");
    SyncEngine engine(fx.store, fx.fake, fx.emlDir);
    UltraNetMailOptions opts;
    engine.SyncFolders("erika", "imaps://x/", opts);
    engine.SyncMessages("erika", "INBOX", "imaps://x/", opts);
    // An interrupted sync stored uid 3 (newest first) but never uid 2: the
    // highest UID is 3, so every later incremental fetch starts above it.
    REQUIRE(fx.store.RemoveMessage("erika", "INBOX", 2).success);
    REQUIRE(!HasUid(fx.store, 2));
    engine.SyncMessages("erika", "INBOX", "imaps://x/", opts);
    REQUIRE(!HasUid(fx.store, 2));            // the incremental fetch cannot reach it

    ServerListsInbox(fx.fake);
    SyncOutcome r = engine.RefreshFolder("erika", "INBOX", "imaps://x/", opts,
                                         /*fetchBodies=*/true);
    REQUIRE(r.ok);
    REQUIRE_EQ(r.stats.repaired, 1);
    REQUIRE(HasUid(fx.store, 2));
    REQUIRE_EQ(SubjectOf(fx.store, 2), std::string("Re: thanks"));
    REQUIRE(fs::exists(engine.BodyPath("erika", "INBOX", 2)));   // and its body

    // Nothing is missing now: a second refresh repairs nothing.
    r = engine.RefreshFolder("erika", "INBOX", "imaps://x/", opts);
    REQUIRE(r.ok);
    REQUIRE_EQ(r.stats.repaired, 0);
}

TEST(refresh_folder_fetches_rows_stored_blank_again) {
    Fixture fx("refresh-blank");
    SyncEngine engine(fx.store, fx.fake, fx.emlDir);
    UltraNetMailOptions opts;
    engine.SyncFolders("erika", "imaps://x/", opts);
    engine.SyncMessages("erika", "INBOX", "imaps://x/", opts);
    // How an earlier version stored uid 2 when its header could not be read.
    MessageEnvelope blank;
    blank.accountId = "erika"; blank.folder = "INBOX"; blank.uid = 2;
    REQUIRE(fx.store.UpsertMessage(blank).success);
    std::vector<int64_t> blanks;
    REQUIRE(fx.store.ListBlankUids("erika", "INBOX", blanks).success);
    REQUIRE_EQ(blanks.size(), (size_t)1);
    REQUIRE_EQ(blanks[0], (int64_t)2);

    ServerListsInbox(fx.fake);
    SyncOutcome r = engine.RefreshFolder("erika", "INBOX", "imaps://x/", opts);
    REQUIRE(r.ok);
    REQUIRE_EQ(SubjectOf(fx.store, 2), std::string("Re: thanks"));
    REQUIRE(fx.store.ListBlankUids("erika", "INBOX", blanks).success);
    REQUIRE(blanks.empty());
}

TEST(sync_messages_drops_a_cache_ahead_of_the_servers_uidnext) {
    Fixture fx("uidnext");
    SyncEngine engine(fx.store, fx.fake, fx.emlDir);
    UltraNetMailOptions opts;
    engine.SyncFolders("erika", "imaps://x/", opts);
    // UIDVALIDITY never known (an older server, or one read wrongly): the
    // renumber check alone cannot tell the cache is stale.
    engine.SyncMessages("erika", "INBOX", "imaps://x/", opts);
    REQUIRE(HasUid(fx.store, 3));

    // The mailbox now numbers its mail 1 and 2, and will hand out 3 next.
    // Holding UID 3 already, the incremental fetch would ask for "4:*" and
    // never see the new mail - nor would the stale subject of uid 1 change.
    fx.fake.uidValidity = 7;
    fx.fake.uidNext = 3;
    fx.fake.envelopes["INBOX"] = {
        Env(1, "Reddit <noreply@redditmail.com>", {"erika@example.com"}, "gpt6.1 sol",
            UltraNetMailFlags::None),
        Env(2, "Hetzner <noreply@hetzner.com>",   {"erika@example.com"}, "Verification code",
            UltraNetMailFlags::None),
    };
    SyncOutcome r = engine.SyncMessages("erika", "INBOX", "imaps://x/", opts);
    REQUIRE(r.ok);
    REQUIRE(r.stats.cacheReset);
    REQUIRE_EQ(r.stats.serverMessages, 2);
    REQUIRE(!HasUid(fx.store, 3));
    REQUIRE_EQ(SubjectOf(fx.store, 1), std::string("gpt6.1 sol"));
    REQUIRE_EQ(SubjectOf(fx.store, 2), std::string("Verification code"));

    // Consistent now: the next sync keeps the cache.
    fx.fake.uidNext = 3;
    r = engine.SyncMessages("erika", "INBOX", "imaps://x/", opts);
    REQUIRE(!r.stats.cacheReset);
    REQUIRE(HasUid(fx.store, 1));
}

TEST(sync_messages_ignores_the_servers_echo_of_the_highest_uid) {
    Fixture fx("echo");
    SyncEngine engine(fx.store, fx.fake, fx.emlDir);
    UltraNetMailOptions opts;
    engine.SyncFolders("erika", "imaps://x/", opts);
    engine.SyncMessages("erika", "INBOX", "imaps://x/", opts, /*fetchBodies=*/true);
    const int bodiesBefore = fx.fake.fetchBodiesCalls;

    // No new mail; the server still answers "UID 4:*" with uid 3.
    fx.fake.echoHighest = true;
    SyncOutcome r = engine.SyncMessages("erika", "INBOX", "imaps://x/", opts,
                                        /*fetchBodies=*/true);
    REQUIRE(r.ok);
    REQUIRE_EQ(r.stats.messages, 0);          // not stored again
    REQUIRE_EQ(fx.fake.fetchBodiesCalls, bodiesBefore);   // nor its body downloaded again
}

TEST(sync_now_follows_mail_deleted_and_read_elsewhere) {
    Fixture fx("syncnow-reconcile");
    SyncService svc(fx.store, fx.fake, fx.emlDir);
    UltraNetMailOptions opts;
    REQUIRE(svc.SyncNow("erika", "imaps://x/", opts).ok);
    REQUIRE(HasUid(fx.store, 3));
    REQUIRE(!(FlagsFor(fx.store, 1) & Flag_Seen));

    // On another computer: uid 1 read, uid 3 moved to a folder.
    fx.fake.serverFlags["INBOX"] = {
        { 1u, UltraNetMailFlags::Seen },
        { 2u, UltraNetMailFlags::Answered },
    };
    SyncOutcome r = svc.SyncNow("erika", "imaps://x/", opts);
    REQUIRE(r.ok);
    REQUIRE_EQ(r.stats.expunged, 1);
    REQUIRE(!HasUid(fx.store, 3));
    REQUIRE(FlagsFor(fx.store, 1) & Flag_Seen);
}

TEST(fetch_envelopes_by_uid_default_serves_only_the_uids_asked_for) {
    FakeMailbox fake;
    fake.envelopes["INBOX"] = {
        Env(5, "a <a@x.com>", {"erika@example.com"}, "five",  UltraNetMailFlags::None),
        Env(7, "b <b@x.com>", {"erika@example.com"}, "seven", UltraNetMailFlags::None),
        Env(9, "c <c@x.com>", {"erika@example.com"}, "nine",  UltraNetMailFlags::None),
    };
    std::vector<uint32_t> got;
    UltraNetMailOptions opts;
    REQUIRE(fake.FetchEnvelopesByUid("imaps://x/", "INBOX", {9, 5, 6},
            [&got](const UltraNetMailEnvelope& e) { got.push_back(e.uid); }, opts).success);
    REQUIRE_EQ(got.size(), (size_t)2);         // 6 is not on the server
    REQUIRE(std::find(got.begin(), got.end(), 5u) != got.end());
    REQUIRE(std::find(got.begin(), got.end(), 9u) != got.end());
}

TEST(refresh_folder_downloads_a_body_an_earlier_download_missed) {
    Fixture fx("refresh-body");
    SyncEngine engine(fx.store, fx.fake, fx.emlDir);
    UltraNetMailOptions opts;
    engine.SyncFolders("erika", "imaps://x/", opts);
    // uid 2's body fails to download the first time.
    const std::string body2 = fx.fake.bodies["INBOX/2"];
    fx.fake.bodies.erase("INBOX/2");
    engine.SyncMessages("erika", "INBOX", "imaps://x/", opts, /*fetchBodies=*/true);
    REQUIRE(!fs::exists(engine.BodyPath("erika", "INBOX", 2)));
    REQUIRE(fs::exists(engine.BodyPath("erika", "INBOX", 1)));

    // The next sync gets it; the bodies it already has are not asked for.
    fx.fake.bodies["INBOX/2"] = body2;
    ServerListsInbox(fx.fake);
    SyncOutcome r = engine.RefreshFolder("erika", "INBOX", "imaps://x/", opts,
                                         /*fetchBodies=*/true);
    REQUIRE(r.ok);
    REQUIRE_EQ(r.stats.bodies, 1);
    REQUIRE(fs::exists(engine.BodyPath("erika", "INBOX", 2)));
    REQUIRE_EQ(fx.fake.lastBodyUids.size(), (size_t)1);
    REQUIRE_EQ(fx.fake.lastBodyUids[0], 2u);
}

// ---- The folder list follows the server -------------------------------------

namespace {
bool HasFolder(LocalStore& s, const std::string& name) {
    std::vector<Folder> folders;
    s.ListFolders("erika", folders);
    for (const auto& f : folders) if (f.name == name) return true;
    return false;
}
size_t CountIn(LocalStore& s, const std::string& folder) {
    std::vector<MessageEnvelope> msgs;
    s.ListMessages("erika", folder, 0, msgs);
    return msgs.size();
}
} // namespace

TEST(sync_folders_drops_folders_the_server_no_longer_lists) {
    Fixture fx("folders-gone");
    SyncEngine engine(fx.store, fx.fake, fx.emlDir);
    UltraNetMailOptions opts;
    fx.fake.folders.push_back(MakeFolder("Projects", ""));
    fx.fake.envelopes["Projects"] = {
        Env(1, "Ann <ann@x.com>", {"erika@example.com"}, "Plan", UltraNetMailFlags::Seen) };
    fx.fake.bodies["Projects/1"] = BuildRaw("Ann <ann@x.com>", "Plan", "the plan");
    REQUIRE(engine.SyncFolders("erika", "imaps://x/", opts).ok);
    engine.SyncMessages("erika", "Projects", "imaps://x/", opts, /*fetchBodies=*/true);
    REQUIRE_EQ(CountIn(fx.store, "Projects"), (size_t)1);
    REQUIRE(fs::exists(engine.BodyPath("erika", "Projects", 1)));

    // A list that fails, or one that names nothing, removes nothing.
    fx.fake.listFoldersFails = true;
    REQUIRE(!engine.SyncFolders("erika", "imaps://x/", opts).ok);
    REQUIRE(HasFolder(fx.store, "Projects"));
    fx.fake.listFoldersFails = false;
    fx.fake.folders.clear();
    REQUIRE(engine.SyncFolders("erika", "imaps://x/", opts).ok);
    REQUIRE(HasFolder(fx.store, "Projects"));

    // Renamed on the server: the old name goes with its mail and bodies.
    fx.fake.folders = { MakeFolder("INBOX", "inbox"), MakeFolder("Sent", "sent"),
                        MakeFolder("Projects 2026", "") };
    SyncOutcome r = engine.SyncFolders("erika", "imaps://x/", opts);
    REQUIRE(r.ok);
    REQUIRE_EQ(r.stats.foldersRemoved, 1);
    REQUIRE(!HasFolder(fx.store, "Projects"));
    REQUIRE(HasFolder(fx.store, "Projects 2026"));
    REQUIRE_EQ(CountIn(fx.store, "Projects"), (size_t)0);
    REQUIRE(!fs::exists(engine.BodyPath("erika", "Projects", 1)));

    // The inbox stays, and its mail with it, whatever a list says.
    engine.SyncMessages("erika", "INBOX", "imaps://x/", opts);
    REQUIRE_EQ(CountIn(fx.store, "INBOX"), (size_t)3);
    fx.fake.folders = { MakeFolder("Sent", "sent") };
    REQUIRE(engine.SyncFolders("erika", "imaps://x/", opts).ok);
    REQUIRE(HasFolder(fx.store, "INBOX"));
    REQUIRE_EQ(CountIn(fx.store, "INBOX"), (size_t)3);
}

TEST(sync_folders_keeps_the_separator_and_the_numbering) {
    Fixture fx("folders-delim");
    SyncEngine engine(fx.store, fx.fake, fx.emlDir);
    UltraNetMailOptions opts;
    UltraNetMailFolder drafts = MakeFolder("INBOX.Drafts", "drafts");
    drafts.delimiter = ".";
    fx.fake.folders.push_back(drafts);
    REQUIRE(engine.SyncFolders("erika", "imaps://x/", opts).ok);
    std::vector<Folder> folders;
    fx.store.ListFolders("erika", folders);
    for (const auto& f : folders)
        if (f.name == "INBOX.Drafts") REQUIRE_EQ(f.delimiter, std::string("."));

    // The folder list used to write UIDVALIDITY 0 over the stored one before
    // every inbox sync, so a renumbered inbox was never noticed in the
    // account sync (folders, then the inbox).
    fx.fake.uidValidity = 100;
    SyncService svc(fx.store, fx.fake, fx.emlDir);
    REQUIRE(svc.SyncNow("erika", "imaps://x/", opts).ok);
    int64_t stored = 0;
    fx.store.GetFolderUidValidity("erika", "INBOX", stored);
    REQUIRE_EQ(stored, (int64_t)100);
    REQUIRE(engine.SyncFolders("erika", "imaps://x/", opts).ok);
    fx.store.GetFolderUidValidity("erika", "INBOX", stored);
    REQUIRE_EQ(stored, (int64_t)100);

    fx.fake.uidValidity = 200;   // renumbered: the same mail under new UIDs
    fx.fake.envelopes["INBOX"] = {
        Env(10, "Boss <boss@acme.com>", {"erika@example.com"}, "Please reply", UltraNetMailFlags::None) };
    SyncOutcome r = svc.SyncNow("erika", "imaps://x/", opts);
    REQUIRE(r.ok);
    REQUIRE(r.stats.cacheReset);
    REQUIRE(!HasUid(fx.store, 1));
    REQUIRE(HasUid(fx.store, 10));
}

// A folder opened after it was deleted on the server: the failure to open it
// is answered by the folder list, which drops it - not by an error alert.
TEST(folder_still_listed_drops_a_folder_deleted_on_the_server) {
    Fixture fx("folder-gone");
    SyncEngine engine(fx.store, fx.fake, fx.emlDir);
    UltraNetMailOptions opts;
    fx.fake.folders.push_back(MakeFolder("Investor", ""));
    REQUIRE(engine.SyncFolders("erika", "imaps://x/", opts).ok);
    REQUIRE(engine.FolderStillListed("erika", "Investor", "imaps://x/", opts));

    fx.fake.folders.pop_back();                     // deleted on the server
    REQUIRE(!engine.FolderStillListed("erika", "Investor", "imaps://x/", opts));
    REQUIRE(!HasFolder(fx.store, "Investor"));
    // A list that cannot be read says nothing is gone; the inbox never is.
    fx.fake.listFoldersFails = true;
    REQUIRE(engine.FolderStillListed("erika", "Investor", "imaps://x/", opts));
    REQUIRE(engine.FolderStillListed("erika", "INBOX", "imaps://x/", opts));
}
