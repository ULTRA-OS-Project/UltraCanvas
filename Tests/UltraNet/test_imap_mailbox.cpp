// Tests/UltraNet/test_imap_mailbox.cpp
// Unit tests for the IMAP mailbox layer: the pure wire parsers in ImapParse.h
// (SEARCH / LIST / STATUS / FETCH FLAGS / header parsing, flag conversion and
// SPECIAL-USE role detection), plus a check that the IMAP plug-in exposes the
// IMailboxProtocolPlugin interface and rejects malformed server URLs. The pure
// tests need no server; the interface test loads the built plug-in DSO.
#include "test_framework.h"

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetPlugins.h>

// Pure parsers live in the plug-in's header — include it directly.
#include "../../UltraCanvas/Plugins/UltraNet/imap/ImapParse.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace ultranet_imap;

// ---- pure parser tests -----------------------------------------------------

TEST(imap_parse_search_uids) {
    auto uids = ParseSearchUids("* SEARCH 1 2 3 5 8 13\r\na1 OK SEARCH completed\r\n");
    REQUIRE_EQ(uids.size(), (size_t)6);
    REQUIRE_EQ(uids.front(), (uint32_t)1);
    REQUIRE_EQ(uids.back(), (uint32_t)13);

    REQUIRE_EQ(ParseSearchUids("* SEARCH\r\n").size(), (size_t)0);   // empty mailbox
}

TEST(imap_flag_roundtrip) {
    UltraNetMailFlags f = UltraNetMailFlags::Seen | UltraNetMailFlags::Answered;
    std::string s = FlagsToImapString(f);
    CHECK(s.find("\\Seen") != std::string::npos);
    CHECK(s.find("\\Answered") != std::string::npos);

    UltraNetMailFlags back = ImapStringToFlags(s);
    REQUIRE(UltraNetHasFlag(back, UltraNetMailFlags::Seen));
    REQUIRE(UltraNetHasFlag(back, UltraNetMailFlags::Answered));
    REQUIRE(!UltraNetHasFlag(back, UltraNetMailFlags::Deleted));
}

TEST(imap_parse_fetch_flags) {
    auto f = ParseFetchFlags("* 12 FETCH (UID 4827 FLAGS (\\Seen \\Answered))");
    REQUIRE(UltraNetHasFlag(f, UltraNetMailFlags::Seen));
    REQUIRE(UltraNetHasFlag(f, UltraNetMailFlags::Answered));
    REQUIRE(!UltraNetHasFlag(f, UltraNetMailFlags::Flagged));

    auto none = ParseFetchFlags("* 1 FETCH (UID 5 FLAGS ())");
    REQUIRE_EQ(static_cast<uint32_t>(none), (uint32_t)0);
}

TEST(imap_parse_list_response_with_roles) {
    const std::string body =
        "* LIST (\\HasNoChildren) \"/\" \"INBOX\"\r\n"
        "* LIST (\\HasNoChildren \\Sent) \"/\" \"INBOX/Sent\"\r\n"
        "* LIST (\\Noselect \\HasChildren) \"/\" \"[Gmail]\"\r\n"
        "* LIST (\\All \\HasNoChildren) \"/\" \"[Gmail]/All Mail\"\r\n"
        "a1 OK LIST completed\r\n";
    auto folders = ParseListResponse(body);
    REQUIRE_EQ(folders.size(), (size_t)4);

    REQUIRE_EQ(folders[0].name, std::string("INBOX"));
    REQUIRE_EQ(folders[0].role, std::string("inbox"));      // name-based
    REQUIRE_EQ(folders[0].delimiter, std::string("/"));

    REQUIRE_EQ(folders[1].name, std::string("INBOX/Sent"));
    REQUIRE_EQ(folders[1].role, std::string("sent"));       // \Sent attribute

    REQUIRE_EQ(folders[2].name, std::string("[Gmail]"));
    REQUIRE(!folders[2].selectable);                        // \Noselect

    REQUIRE_EQ(folders[3].role, std::string("all"));        // \All attribute
}

TEST(imap_detect_role_name_fallback) {
    std::vector<std::string> none;
    REQUIRE_EQ(DetectFolderRole(none, "Junk"), std::string("junk"));
    REQUIRE_EQ(DetectFolderRole(none, "Deleted Items"), std::string("trash"));
    REQUIRE_EQ(DetectFolderRole(none, "Work/Projects"), std::string(""));
    std::vector<std::string> attr = {"\\HasNoChildren", "\\Drafts"};
    REQUIRE_EQ(DetectFolderRole(attr, "Entwürfe"), std::string("drafts")); // attr wins
}

TEST(imap_parse_status_response) {
    auto st = ParseStatusResponse(
        "* STATUS \"INBOX\" (MESSAGES 231 RECENT 0 UIDNEXT 44292 UIDVALIDITY 1 UNSEEN 3)\r\n");
    REQUIRE_EQ(st.messages, (uint32_t)231);
    REQUIRE_EQ(st.uidNext, (uint32_t)44292);
    REQUIRE_EQ(st.uidValidity, (uint32_t)1);
    REQUIRE_EQ(st.unseen, (uint32_t)3);
}

TEST(imap_parse_envelope_headers) {
    const std::string headers =
        "From: Anna Schmidt <anna@example.com>\r\n"
        "To: erika@example.com, team@example.com\r\n"
        "Subject: Re: Meeting notes\r\n"
        "Date: Tue, 14 Jan 2026 14:02:00 +0100\r\n"
        "Message-ID: <abc123@example.com>\r\n"
        "In-Reply-To: <prev456@example.com>\r\n";
    UltraNetMailEnvelope env;
    ParseEnvelopeHeaders(headers, env);
    REQUIRE_EQ(env.from, std::string("Anna Schmidt <anna@example.com>"));
    REQUIRE_EQ(env.to.size(), (size_t)2);
    REQUIRE_EQ(env.to[0], std::string("erika@example.com"));
    REQUIRE_EQ(env.subject, std::string("Re: Meeting notes"));
    REQUIRE_EQ(env.messageId, std::string("<abc123@example.com>"));
    REQUIRE_EQ(env.inReplyTo, std::string("<prev456@example.com>"));
}

TEST(imap_parse_folded_header) {
    // A Subject folded across two lines (RFC 5322 §2.2.3) must be unfolded.
    const std::string headers =
        "Subject: This is a very long subject line that has been\r\n"
        " folded onto a second line\r\n";
    UltraNetMailEnvelope env;
    ParseEnvelopeHeaders(headers, env);
    CHECK(env.subject.find("folded onto a second line") != std::string::npos);
    CHECK(env.subject.find("has been folded") != std::string::npos);
}

// ---- plug-in interface test (loads the DSO if available) -------------------

namespace {
fs::path ImapPluginPath() {
    if (const char* env = std::getenv("ULTRANET_IMAP_PLUGIN_PATH"))
        if (fs::exists(env)) return env;
#ifdef ULTRANET_IMAP_PLUGIN_PATH_DEFINE
    if (fs::exists(ULTRANET_IMAP_PLUGIN_PATH_DEFINE))
        return fs::path{ULTRANET_IMAP_PLUGIN_PATH_DEFINE};
#endif
    return {};
}
} // namespace

TEST(imap_plugin_exposes_mailbox_interface) {
    const fs::path p = ImapPluginPath();
    if (p.empty()) SKIP("IMAP plug-in DSO not available in this env");

    UltraNet_Initialize();
    UltraNet_SetPluginDirectory(p.parent_path().string());
    UltraNet_RefreshPlugins();

    auto plugin = UltraNet_GetPlugin("imaps");
    REQUIRE(plugin != nullptr);
    auto* mailbox = dynamic_cast<IMailboxProtocolPlugin*>(plugin.get());
    REQUIRE(mailbox != nullptr);   // the richer interface is exposed

    // A malformed server URL must be rejected without touching the network.
    std::vector<UltraNetMailFolder> folders;
    UltraNetMailOptions opt;
    auto r = mailbox->ListFolders("http://not-imap/", folders, opt);
    CHECK(!bool(r));
    REQUIRE_EQ(r.code, UltraNetResultCode::InvalidUrl);
}
