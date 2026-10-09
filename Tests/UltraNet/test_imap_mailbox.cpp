// Tests/UltraNet/test_imap_mailbox.cpp
// Unit tests for the IMAP mailbox layer: the pure wire parsers in ImapParse.h
// (SEARCH / LIST / STATUS / FETCH FLAGS / header parsing, flag conversion and
// SPECIAL-USE role detection), plus a check that the IMAP plug-in exposes the
// IMailboxProtocolPlugin interface and rejects malformed server URLs. The pure
// tests need no server; the interface test loads the built plug-in DSO.
#include "test_framework.h"

#if !defined(_WIN32) && !defined(_WIN64)
#include <dlfcn.h>   // dlsym(RTLD_DEFAULT) in the scope test
#endif
#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetPlugins.h>
#include <UltraNet/UltraNetMime.h>   // UltraNet_ImapUtf7Decode

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

TEST(imap_raw_header_value_reads_the_header_block_only) {
    const std::string raw =
        "From: Erika <erika@example.com>\r\n"
        "message-id:   <abc@example.com> \r\n"
        "Subject: a long\r\n"
        "  folded subject\r\n"
        "\r\n"
        "Message-ID: <in-the-body@example.com>\r\n";
    REQUIRE_EQ(RawHeaderValue(raw, "Message-ID"), std::string("<abc@example.com>"));
    REQUIRE_EQ(RawHeaderValue(raw, "Subject"), std::string("a long  folded subject"));
    REQUIRE_EQ(RawHeaderValue(raw, "Bcc"), std::string());
    REQUIRE_EQ(RawHeaderValue("Subject: x\n\nbody", "Subject"), std::string("x"));
}

TEST(imap_search_by_message_id_quotes_the_id) {
    REQUIRE_EQ(SearchByMessageIdCommand("<abc@example.com>"),
               std::string("UID SEARCH HEADER Message-ID \"<abc@example.com>\""));
    REQUIRE_EQ(SearchByMessageIdCommand("<a\"b\\c@x>"),
               std::string("UID SEARCH HEADER Message-ID \"<a\\\"b\\\\c@x>\""));
}

TEST(imap_uid_expunge_names_one_message) {
    REQUIRE_EQ(UidExpungeCommand(42), std::string("UID EXPUNGE 42"));
}

// Reading a message must not mark it read: the plug-in reads the flags first
// and takes \Seen off again for an unread message (FetchKeepingUnread).
TEST(imap_keep_unread_commands) {
    REQUIRE_EQ(UidFetchFlagsCommand(42), std::string("UID FETCH 42 (FLAGS)"));
    REQUIRE_EQ(UidMarkUnreadCommand(42), std::string("UID STORE 42 -FLAGS.SILENT (\\Seen)"));
    REQUIRE(HasFetchFlags("* 3 FETCH (UID 42 FLAGS ())\r\n"));
    REQUIRE(HasFetchFlags("* 3 FETCH (UID 42 FLAGS (\\Seen))\r\n"));
    REQUIRE(!HasFetchFlags(""));   // no answer: the state is unknown, not "unread"
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

TEST(imap_parse_all_flags) {
    // A whole "UID FETCH 1:* (FLAGS)" response: one (uid, flags) pair per line,
    // UID before or after the FLAGS group (Gmail returns either order).
    const std::string body =
        "* 1 FETCH (UID 100 FLAGS (\\Seen))\r\n"
        "* 2 FETCH (FLAGS (\\Seen \\Flagged) UID 101)\r\n"
        "* 3 FETCH (UID 102 FLAGS ())\r\n"
        "a1 OK FETCH completed\r\n";
    auto pairs = ParseAllFlags(body);
    REQUIRE_EQ(pairs.size(), (size_t)3);
    REQUIRE_EQ(pairs[0].first, (uint32_t)100);
    REQUIRE(UltraNetHasFlag(pairs[0].second, UltraNetMailFlags::Seen));
    REQUIRE_EQ(pairs[1].first, (uint32_t)101);
    REQUIRE(UltraNetHasFlag(pairs[1].second, UltraNetMailFlags::Flagged));
    REQUIRE_EQ(pairs[2].first, (uint32_t)102);
    REQUIRE_EQ(static_cast<uint32_t>(pairs[2].second), (uint32_t)0);

    // Empty folder → no data lines → no pairs (caller must not read this as a
    // failure and expunge everything; that decision lives above the parser).
    REQUIRE_EQ(ParseAllFlags("a1 OK FETCH completed\r\n").size(), (size_t)0);
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

TEST(imap_modified_utf7_decode) {
    // Plain ASCII passes through unchanged (fast path, no '&').
    REQUIRE_EQ(UltraNet_ImapUtf7Decode("INBOX"), std::string("INBOX"));
    REQUIRE_EQ(UltraNet_ImapUtf7Decode("[Gmail]/Sent Mail"),
               std::string("[Gmail]/Sent Mail"));

    // "&-" is a literal ampersand.
    REQUIRE_EQ(UltraNet_ImapUtf7Decode("&-"), std::string("&"));

    // "&<mbase64>-" decodes UTF-16BE code units to UTF-8.
    REQUIRE_EQ(UltraNet_ImapUtf7Decode("&AOk-"), std::string("\xC3\xA9"));       // é U+00E9
    REQUIRE_EQ(UltraNet_ImapUtf7Decode("&IKw-"), std::string("\xE2\x82\xAC"));   // € U+20AC
    REQUIRE_EQ(UltraNet_ImapUtf7Decode("Test&AOk-"), std::string("Test\xC3\xA9"));

    // Malformed (unterminated shift) is returned unchanged — never worse than raw.
    REQUIRE_EQ(UltraNet_ImapUtf7Decode("&AOk"), std::string("&AOk"));
}

TEST(imap_detect_role_name_fallback) {
    std::vector<std::string> none;
    REQUIRE_EQ(DetectFolderRole(none, "Junk"), std::string("junk"));
    REQUIRE_EQ(DetectFolderRole(none, "Deleted Items"), std::string("trash"));
    REQUIRE_EQ(DetectFolderRole(none, "Work/Projects"), std::string(""));
    std::vector<std::string> attr = {"\\HasNoChildren", "\\Drafts"};
    REQUIRE_EQ(DetectFolderRole(attr, "Entwürfe"), std::string("drafts")); // attr wins
}

// Courier-style servers put every folder under "INBOX." - and a folder carried
// over from a server with another separator keeps it as "^": the Sent folder
// of an account showed as "INBOX.INBOX^Sent", with no Sent role.
TEST(imap_detect_role_by_the_servers_separator) {
    std::vector<std::string> none;
    REQUIRE_EQ(DetectFolderRole(none, "INBOX.Drafts", "."), std::string("drafts"));
    REQUIRE_EQ(DetectFolderRole(none, "INBOX.Trash", "."), std::string("trash"));
    REQUIRE_EQ(DetectFolderRole(none, "INBOX.INBOX^Sent", "."), std::string("sent"));
    REQUIRE_EQ(DetectFolderRole(none, "INBOX.Investor", "."), std::string(""));
    // German names, and the modified UTF-7 of the ones outside ASCII.
    REQUIRE_EQ(DetectFolderRole(none, "Gesendete Objekte", "/"), std::string("sent"));
    REQUIRE_EQ(DetectFolderRole(none, "INBOX.Papierkorb", "."), std::string("trash"));
    REQUIRE_EQ(DetectFolderRole(none, "Entw&APw-rfe", "/"), std::string("drafts"));
    REQUIRE_EQ(DetectFolderRole(none, "Gel&APY-schte Objekte", "/"), std::string("trash"));
    // Only INBOX itself is the inbox.
    REQUIRE_EQ(DetectFolderRole(none, "inbox", "/"), std::string("inbox"));
    REQUIRE_EQ(DetectFolderRole(none, "Archive/Inbox", "/"), std::string(""));
    // A dot in a name is not a level where the separator is '/'.
    REQUIRE_EQ(DetectFolderRole(none, "Mr. Sent", "/"), std::string(""));
    // The separator comes from the LIST line.
    UltraNetMailFolder f;
    REQUIRE(ParseListLine("* LIST (\\HasNoChildren) \".\" \"INBOX.INBOX^Sent\"", f));
    REQUIRE_EQ(f.delimiter, std::string("."));
    REQUIRE_EQ(f.role, std::string("sent"));
}

TEST(imap_parse_status_response) {
    auto st = ParseStatusResponse(
        "* STATUS \"INBOX\" (MESSAGES 231 RECENT 0 UIDNEXT 44292 UIDVALIDITY 1 UNSEEN 3)\r\n");
    REQUIRE_EQ(st.messages, (uint32_t)231);
    REQUIRE_EQ(st.uidNext, (uint32_t)44292);
    REQUIRE_EQ(st.uidValidity, (uint32_t)1);
    REQUIRE_EQ(st.unseen, (uint32_t)3);
}

// Values above 2147483647 are valid IMAP numbers (unsigned 32-bit). strtol's
// `long` is 32 bits on Windows, where every such UIDVALIDITY used to read as
// 2147483647 - so a renumbered mailbox looked unchanged there and only there.
TEST(imap_parse_numbers_above_int32) {
    auto st = ParseStatusResponse(
        "* STATUS \"INBOX\" (MESSAGES 59 RECENT 0 UIDNEXT 3000000123 "
        "UIDVALIDITY 4294967295 UNSEEN 11)\r\n");
    REQUIRE_EQ(st.uidNext, (uint32_t)3000000123u);
    REQUIRE_EQ(st.uidValidity, (uint32_t)4294967295u);
    REQUIRE_EQ(st.messages, (uint32_t)59);

    auto uids = ParseSearchUids("* SEARCH 2147483648 4294967295\r\n");
    REQUIRE_EQ(uids.size(), (size_t)2);
    REQUIRE_EQ(uids[0], (uint32_t)2147483648u);
    REQUIRE_EQ(uids[1], (uint32_t)4294967295u);
    // Larger than an IMAP number can be: not read as some other UID.
    REQUIRE_EQ(ParseSearchUids("* SEARCH 4294967296\r\n").size(), (size_t)0);

    auto pairs = ParseAllFlags("* 1 FETCH (UID 3000000000 FLAGS (\\Seen))\r\n");
    REQUIRE_EQ(pairs.size(), (size_t)1);
    REQUIRE_EQ(pairs[0].first, (uint32_t)3000000000u);
}

// A mailbox named after a STATUS item does not hide the item's value.
TEST(imap_parse_status_mailbox_named_like_an_item) {
    auto st = ParseStatusResponse(
        "* STATUS \"Recent messages\" (MESSAGES 7 RECENT 2 UIDNEXT 90 "
        "UIDVALIDITY 5 UNSEEN 1)\r\n");
    REQUIRE_EQ(st.messages, (uint32_t)7);
    REQUIRE_EQ(st.recent, (uint32_t)2);
    REQUIRE_EQ(st.uidNext, (uint32_t)90);
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

// ---- batched fetches: responses with literals ----------------------------

TEST(imap_literal_at_line_end) {
    std::size_t n = 0;
    REQUIRE(LiteralAtLineEnd("* 1 FETCH (UID 7 BODY[HEADER] {342}", n));
    REQUIRE_EQ(n, (std::size_t)342);
    REQUIRE(LiteralAtLineEnd("A1 APPEND x {12+}", n));
    REQUIRE_EQ(n, (std::size_t)12);
    REQUIRE(LiteralAtLineEnd("* 2 FETCH (BINARY[] ~{0}", n));
    REQUIRE_EQ(n, (std::size_t)0);
    REQUIRE(!LiteralAtLineEnd("* OK [UIDNEXT 161] Predicted next UID", n));
    REQUIRE(!LiteralAtLineEnd("* 3 FETCH (UID 9 FLAGS (\\Seen))", n));
    REQUIRE(!LiteralAtLineEnd("a {} b {1a}", n));
    REQUIRE(!LiteralAtLineEnd("}", n));
}

TEST(imap_reader_takes_literals_whole_however_they_arrive) {
    // Two FETCH responses, the first with a header that holds line breaks and
    // text that looks like a literal of its own, then the tagged completion.
    const std::string header1 = "From: A <a@x.example>\r\nSubject: costs {5}\r\n\r\n";
    const std::string header2 = "From: B <b@x.example>\r\n\r\n";
    const std::string wire =
        "* 1 FETCH (UID 7 FLAGS (\\Seen) BODY[HEADER] {" + std::to_string(header1.size()) + "}\r\n" +
        header1 + ")\r\n" +
        "* 2 FETCH (UID 9 FLAGS () BODY[HEADER] {" + std::to_string(header2.size()) + "}\r\n" +
        header2 + ")\r\n" +
        "U3 OK Fetch completed.\r\n";
    // Fed a byte at a time, as a slow connection might hand it over.
    ImapResponseReader reader;
    std::vector<ImapResponse> got;
    ImapResponse r;
    for (char c : wire) {
        reader.Feed(&c, 1);
        while (reader.Next(r)) got.push_back(r);
    }
    REQUIRE_EQ(got.size(), (std::size_t)3);
    REQUIRE_EQ(got[0].literals.size(), (std::size_t)1);
    REQUIRE_EQ(got[0].literals[0], header1);
    REQUIRE_EQ(got[0].segments.size(), (std::size_t)2);
    REQUIRE_EQ(got[0].segments[1], std::string(")"));
    REQUIRE_EQ(got[1].literals[0], header2);
    REQUIRE(got[2].IsTagged("U3"));
    REQUIRE(!got[2].IsTagged("U30"));
    REQUIRE_EQ(got[2].Status(), std::string("OK"));
    REQUIRE_EQ(reader.Pending(), (std::size_t)0);

    // All at once, and an empty literal.
    ImapResponseReader whole;
    whole.Feed(wire.data(), wire.size());
    int count = 0;
    while (whole.Next(r)) ++count;
    REQUIRE_EQ(count, 3);
    ImapResponseReader empty;
    const std::string e = "* 4 FETCH (UID 11 BODY[] {0}\r\n)\r\nU4 NO gone\r\n";
    empty.Feed(e.data(), e.size());
    REQUIRE(empty.Next(r));
    REQUIRE_EQ(r.literals.size(), (std::size_t)1);
    REQUIRE(r.literals[0].empty());
    REQUIRE(empty.Next(r));
    REQUIRE_EQ(r.Status(), std::string("NO"));
}

TEST(imap_parse_fetch_response) {
    ImapResponse r;
    r.segments = { "* 12 FETCH (UID 4711 FLAGS (\\Seen \\Answered) BODY[HEADER] {9}", ")" };
    r.literals = { "Subject: x" };
    ImapFetchItem item;
    REQUIRE(ParseFetchResponse(r, item));
    REQUIRE_EQ(item.uid, (uint32_t)4711);
    REQUIRE(item.hasFlags);
    REQUIRE(UltraNetHasFlag(item.flags, UltraNetMailFlags::Seen));
    REQUIRE(UltraNetHasFlag(item.flags, UltraNetMailFlags::Answered));
    REQUIRE_EQ(item.sections["BODY[HEADER]"], std::string("Subject: x"));

    // The UID after the literal, the section in lower case with an origin, no
    // flags asked for.
    r.segments = { "* 3 fetch (body[]<0> {5}", " UID 99)" };
    r.literals = { "Hello" };
    REQUIRE(ParseFetchResponse(r, item));
    REQUIRE_EQ(item.uid, (uint32_t)99);
    REQUIRE(!item.hasFlags);
    REQUIRE_EQ(item.sections.count("BODY[]"), (std::size_t)1);
    REQUIRE_EQ(item.sections["BODY[]"], std::string("Hello"));

    // A flag change the server sends on its own has no UID: nothing to file.
    r.segments = { "* 5 FETCH (FLAGS (\\Seen))" };
    r.literals.clear();
    REQUIRE(!ParseFetchResponse(r, item));
    // A section the server sent as NIL is simply not there.
    r.segments = { "* 6 FETCH (UID 8 BODY[HEADER] NIL)" };
    REQUIRE(ParseFetchResponse(r, item));
    REQUIRE(item.sections.empty());
    // Not a FETCH at all.
    r.segments = { "* 160 EXISTS" };
    REQUIRE(!ParseFetchResponse(r, item));
    r.segments = { "* SEARCH 1 2 3" };
    REQUIRE(!ParseFetchResponse(r, item));
}

// A folder name with characters a quoted string cannot hold comes as a
// literal; read through the session, the LIST and STATUS parsers get it back as
// one line with the name quoted in.
TEST(imap_response_as_line_feeds_the_line_parsers) {
    const std::string wire =
        "* LIST (\\HasNoChildren) \".\" {14}\r\nINBOX.Rechnung\r\n"
        "* LIST (\\HasNoChildren) \".\" {10}\r\nSay \"hi\"\\x\r\n"
        "* STATUS {5}\r\nINBOX (MESSAGES 59 UIDNEXT 701 UIDVALIDITY 3)\r\n"
        "U1 OK done\r\n";
    ImapResponseReader reader;
    reader.Feed(wire.data(), wire.size());
    std::string lines;
    ImapResponse r;
    while (reader.Next(r)) if (!r.IsTagged("U1")) lines += r.AsLine() + "\r\n";
    const auto folders = ParseListResponse(lines);
    REQUIRE_EQ(folders.size(), (std::size_t)2);
    REQUIRE_EQ(folders[0].name, std::string("INBOX.Rechnung"));
    REQUIRE_EQ(folders[0].delimiter, std::string("."));
    REQUIRE_EQ(folders[1].name, std::string("Say \"hi\"\\x"));
    const UltraNetMailboxStatus st = ParseStatusResponse(lines);
    REQUIRE_EQ(st.messages, (uint32_t)59);
    REQUIRE_EQ(st.uidNext, (uint32_t)701);
    REQUIRE_EQ(st.uidValidity, (uint32_t)3);
}

TEST(imap_parse_fetch_response_size) {
    ImapResponse r;
    r.segments = { "* 4 FETCH (UID 812 RFC822.SIZE 48213)" };
    ImapFetchItem item;
    REQUIRE(ParseFetchResponse(r, item));
    REQUIRE_EQ(item.uid, (uint32_t)812);
    REQUIRE(item.hasSize);
    REQUIRE_EQ(item.size, (uint32_t)48213);
    r.segments = { "* 5 FETCH (UID 813 FLAGS ())" };
    REQUIRE(ParseFetchResponse(r, item));
    REQUIRE(!item.hasSize);
}

TEST(imap_uid_set_string) {
    REQUIRE_EQ(UidSetString({ 5, 3, 4, 9, 7, 8, 1 }), std::string("1,3:5,7:9"));
    REQUIRE_EQ(UidSetString({ 42 }), std::string("42"));
    REQUIRE_EQ(UidSetString({ 2, 2, 3 }), std::string("2:3"));
    REQUIRE_EQ(UidSetString({ 4294967295u, 4294967294u }), std::string("4294967294:4294967295"));
    REQUIRE_EQ(UidSetString({}), std::string(""));
}

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

#if !defined(_WIN32) && !defined(_WIN64)
TEST(plugins_are_loaded_without_joining_the_global_symbol_scope) {
    // Plug-ins are dlopen()ed RTLD_LOCAL: they need nothing from the host's
    // symbol table, and what they export must not bind the symbols of
    // libraries loaded after them. The entry point every plug-in exports is
    // the probe - with RTLD_GLOBAL the process-wide lookup finds the first
    // plug-in's, with RTLD_LOCAL it finds none. (The host does not define it.)
    const fs::path p = ImapPluginPath();
    if (p.empty()) SKIP("IMAP plug-in DSO not available in this env");

    UltraNet_Initialize();
    UltraNet_SetPluginDirectory(p.parent_path().string());
    UltraNet_RefreshPlugins();
    REQUIRE(UltraNet_GetPlugin("imaps") != nullptr);   // it did load

    CHECK(dlsym(RTLD_DEFAULT, "UltraNet_PluginInit") == nullptr);
}
#endif
