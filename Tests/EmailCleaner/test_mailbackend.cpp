// Tests/EmailCleaner/test_mailbackend.cpp
// The outward half of an action, driven against a fake IMAP plug-in: Trash
// resolution and the move itself. No server, no network.
// Version: 0.2.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "EmailCleanerMailBackend.h"

#include <string>
#include <vector>

using namespace EmailCleaner;

namespace {

// A canned mailbox: serves a folder list and records the moves asked of it.
class FakeMailbox : public IMailboxProtocolPlugin {
public:
    std::vector<UltraNetMailFolder> folders;
    bool failListFolders = false;
    bool failMove = false;

    struct MoveCall { std::string src, dest; uint32_t uid; };
    std::vector<MoveCall> moves;
    std::vector<UltraNetMailMessage> sent;
    int listFolderCalls = 0;

    // IUltraNetPlugin
    std::string GetName() const override { return "FakeMailbox"; }
    std::string GetVersion() const override { return "0.0.1"; }
    std::vector<std::string> GetSupportedSchemes() const override { return {"imap", "imaps"}; }
    UltraNetResult Initialize(const UltraNetConfig&) override { return UltraNetResult::Ok(); }
    void Shutdown() override {}

    // IMailProtocolPlugin
    UltraNetResult SendMail(const UltraNetMailMessage& message,
                            const UltraNetMailOptions&) override {
        sent.push_back(message);
        return UltraNetResult::Ok();
    }
    UltraNetResult FetchMessages(const std::string&, std::vector<UltraNetMailMessage>&,
                                 const UltraNetMailOptions&) override {
        return UltraNetResult::Ok();
    }

    // IMailboxProtocolPlugin
    UltraNetResult ListFolders(const std::string&, std::vector<UltraNetMailFolder>& out,
                               const UltraNetMailOptions&) override {
        ++listFolderCalls;
        if (failListFolders)
            return UltraNetResult::Error(UltraNetResultCode::ConnectionRefused, "offline");
        out = folders;
        return UltraNetResult::Ok();
    }
    UltraNetResult GetMailboxStatus(const std::string&, const std::string&,
                                    UltraNetMailboxStatus&, const UltraNetMailOptions&) override {
        return UltraNetResult::Ok();
    }
    UltraNetResult FetchEnvelopes(const std::string&, const std::string&, uint32_t,
                                  std::vector<UltraNetMailEnvelope>&,
                                  const UltraNetMailOptions&) override {
        return UltraNetResult::Ok();
    }
    UltraNetResult FetchMessage(const std::string&, const std::string&, uint32_t,
                                std::string&, const UltraNetMailOptions&) override {
        return UltraNetResult::Error(UltraNetResultCode::NotFound, "no body");
    }
    UltraNetResult StoreFlags(const std::string&, const std::string&, uint32_t,
                              UltraNetMailFlags, bool, const UltraNetMailOptions&) override {
        return UltraNetResult::Ok();
    }
    UltraNetResult MoveMessage(const std::string&, const std::string& src, uint32_t uid,
                               const std::string& dest, const UltraNetMailOptions&) override {
        if (failMove)
            return UltraNetResult::Error(UltraNetResultCode::Unknown, "mailbox full");
        moves.push_back({src, dest, uid});
        return UltraNetResult::Ok();
    }
    UltraNetResult AppendMessage(const std::string&, const std::string&, const std::string&,
                                 UltraNetMailFlags, const UltraNetMailOptions&) override {
        return UltraNetResult::Ok();
    }
};

UltraNetMailFolder Folder(const std::string& name, const std::string& role = "") {
    UltraNetMailFolder f;
    f.name = name;
    f.role = role;
    f.delimiter = "/";
    f.selectable = true;
    return f;
}

MailAccountAccess Access() {
    MailAccountAccess a;
    a.accountId = "erika";
    a.serverUrl = "imaps://mail.example:993/";
    a.ownerAddress = "erika@example.com";
    return a;
}

} // namespace

// ---- Trash resolution ------------------------------------------------------

TEST(MailBackend_PrefersTheServersOwnTrashRole) {
    FakeMailbox mailbox;
    // A server that both declares a role and has a differently-named folder
    // the fallback would otherwise pick.
    mailbox.folders = { Folder("INBOX", "inbox"), Folder("Trash"),
                        Folder("[Gmail]/Bin", "trash") };

    MailBackend backend(mailbox);
    backend.SetAccount(Access());

    std::string error;
    REQUIRE_EQ(backend.ResolveTrashFolder(Access(), error), std::string("[Gmail]/Bin"));
    REQUIRE(error.empty());
}

TEST(MailBackend_FallsBackToWellKnownNames) {
    FakeMailbox mailbox;
    mailbox.folders = { Folder("INBOX", "inbox"), Folder("Deleted Items") };

    MailBackend backend(mailbox);
    std::string error;
    REQUIRE_EQ(backend.ResolveTrashFolder(Access(), error), std::string("Deleted Items"));
}

TEST(MailBackend_RecognisesTrashUnderAPrefixAndInOtherLanguages) {
    REQUIRE(MailBackend::LooksLikeTrash("Trash"));
    REQUIRE(MailBackend::LooksLikeTrash("INBOX.Trash"));
    REQUIRE(MailBackend::LooksLikeTrash("[Gmail]/Bin"));
    REQUIRE(MailBackend::LooksLikeTrash("Deleted Items"));
    REQUIRE(MailBackend::LooksLikeTrash("Papierkorb"));
    REQUIRE(MailBackend::LooksLikeTrash("Corbeille"));

    // Folders that merely mention it are not it.
    REQUIRE(!MailBackend::LooksLikeTrash("INBOX"));
    REQUIRE(!MailBackend::LooksLikeTrash("Trash talk"));
    REQUIRE(!MailBackend::LooksLikeTrash("Archive"));
    REQUIRE(!MailBackend::LooksLikeTrash(""));
}

TEST(MailBackend_RefusesToGuessWhenThereIsNoTrash) {
    // Guessing here would scatter mail into a folder nobody looks in, so the
    // move has to fail loudly instead.
    FakeMailbox mailbox;
    mailbox.folders = { Folder("INBOX", "inbox"), Folder("Archive", "archive") };

    MailBackend backend(mailbox);
    backend.SetAccount(Access());

    std::string error;
    REQUIRE(!backend.MoveToTrash("erika", "INBOX", 1, error));
    REQUIRE(error.find("no Trash folder") != std::string::npos);
    REQUIRE(mailbox.moves.empty());
}

// ---- Moving ----------------------------------------------------------------

TEST(MailBackend_MovesToTheResolvedTrash) {
    FakeMailbox mailbox;
    mailbox.folders = { Folder("INBOX", "inbox"), Folder("Trash", "trash") };

    MailBackend backend(mailbox);
    backend.SetAccount(Access());

    std::string error;
    REQUIRE(backend.MoveToTrash("erika", "INBOX", 42, error));
    REQUIRE_EQ(mailbox.moves.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(mailbox.moves[0].src, std::string("INBOX"));
    REQUIRE_EQ(mailbox.moves[0].dest, std::string("Trash"));
    REQUIRE_EQ(mailbox.moves[0].uid, 42u);
    REQUIRE_EQ(backend.ResolvedTrash("erika"), std::string("Trash"));
}

TEST(MailBackend_ResolvesTrashOnceForManyMoves) {
    // A thousand-message clear-out must not ask for the folder list a
    // thousand times.
    FakeMailbox mailbox;
    mailbox.folders = { Folder("INBOX", "inbox"), Folder("Trash", "trash") };

    MailBackend backend(mailbox);
    backend.SetAccount(Access());

    std::string error;
    for (int uid = 1; uid <= 5; ++uid)
        REQUIRE(backend.MoveToTrash("erika", "INBOX", uid, error));

    REQUIRE_EQ(mailbox.listFolderCalls, 1);
    REQUIRE_EQ(mailbox.moves.size(), static_cast<std::size_t>(5));
}

TEST(MailBackend_MessageAlreadyInTrashIsLeftAlone) {
    FakeMailbox mailbox;
    mailbox.folders = { Folder("INBOX", "inbox"), Folder("Trash", "trash") };

    MailBackend backend(mailbox);
    backend.SetAccount(Access());

    std::string error;
    REQUIRE(backend.MoveToTrash("erika", "Trash", 7, error));
    REQUIRE(mailbox.moves.empty());     // reported as done, nothing asked of the server
}

TEST(MailBackend_UnknownAccountAndServerErrorsAreReported) {
    FakeMailbox mailbox;
    mailbox.folders = { Folder("Trash", "trash") };
    MailBackend backend(mailbox);

    std::string error;
    REQUIRE(!backend.MoveToTrash("nobody", "INBOX", 1, error));
    REQUIRE(error.find("no mail connection") != std::string::npos);

    backend.SetAccount(Access());
    mailbox.failMove = true;
    error.clear();
    REQUIRE(!backend.MoveToTrash("erika", "INBOX", 1, error));
    REQUIRE(error.find("mailbox full") != std::string::npos);
}

TEST(MailBackend_OfflineFolderListIsReportedNotGuessedAround) {
    FakeMailbox mailbox;
    mailbox.failListFolders = true;

    MailBackend backend(mailbox);
    backend.SetAccount(Access());

    std::string error;
    REQUIRE(!backend.MoveToTrash("erika", "INBOX", 1, error));
    REQUIRE(error.find("could not list folders") != std::string::npos);
}

// ---- Unsubscribe -----------------------------------------------------------

TEST(MailBackend_OneClickRefusesPlaintextUrls) {
    // RFC 8058 requires https; the request carries a subscriber token.
    FakeMailbox mailbox;
    MailBackend backend(mailbox);

    std::string error;
    REQUIRE(!backend.PostOneClick("http://list.example/u", error));
    REQUIRE(error.find("https") != std::string::npos);

    error.clear();
    REQUIRE(!backend.PostOneClick("", error));
    REQUIRE(!error.empty());
}

TEST(MailBackend_UnsubscribeMailComesFromTheAccountsOwnAddress) {
    FakeMailbox mailbox;
    MailBackend backend(mailbox);
    backend.SetAccount(Access());

    std::string error;
    REQUIRE(backend.SendUnsubscribeMail("erika", "bye@list.example", "unsub-9", error));
    REQUIRE_EQ(mailbox.sent.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(mailbox.sent[0].from, std::string("erika@example.com"));
    REQUIRE_EQ(mailbox.sent[0].to.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(mailbox.sent[0].to[0], std::string("bye@list.example"));
    REQUIRE_EQ(mailbox.sent[0].subject, std::string("unsub-9"));
}

TEST(MailBackend_UnsubscribeMailNeedsAnAddressToSendFrom) {
    FakeMailbox mailbox;
    MailBackend backend(mailbox);

    MailAccountAccess anonymous = Access();
    anonymous.ownerAddress.clear();
    backend.SetAccount(anonymous);

    std::string error;
    REQUIRE(!backend.SendUnsubscribeMail("erika", "bye@list.example", "x", error));
    REQUIRE(error.find("no address") != std::string::npos);
    REQUIRE(mailbox.sent.empty());

    error.clear();
    REQUIRE(!backend.SendUnsubscribeMail("erika", "", "x", error));
    REQUIRE(error.find("no unsubscribe address") != std::string::npos);
}
