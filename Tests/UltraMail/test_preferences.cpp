// Tests/UltraMail/test_preferences.cpp
// The app-wide preferences behind the Settings window: the remote-image
// policy, trusted websites (domain matching) and the reading options survive
// a save and a load, and an old file keeps the defaults.
// Version: 0.4.0 - folder_tree_content, account_order
// Version: 0.3.0 - trusted_senders, blocked_senders
// Version: 0.2.0 - link_display (status bar / tooltip)
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "test_framework.h"

#include "UltraMailPreferences.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>
#include "../../UltraCanvas/include/UltraCanvasPathUtf8.h"

using namespace UltraMail;

TEST(preferences_normalize_domain) {
    REQUIRE_EQ(Preferences::NormalizeDomain("https://www.Anthropic.com/x?y"), std::string("anthropic.com"));
    REQUIRE_EQ(Preferences::NormalizeDomain("@mail.example.org"), std::string("mail.example.org"));
    REQUIRE_EQ(Preferences::NormalizeDomain("*.example.org"), std::string("example.org"));
    REQUIRE_EQ(Preferences::NormalizeDomain("news@Example.COM"), std::string("example.com"));
    REQUIRE_EQ(Preferences::NormalizeDomain("//cdn.example.net:8080/a.png"), std::string("cdn.example.net"));
    REQUIRE(Preferences::NormalizeDomain("localhost").empty());
    REQUIRE(Preferences::NormalizeDomain("not a domain.com").empty());
}

TEST(preferences_domain_matches_subdomains_only) {
    REQUIRE(Preferences::DomainMatches("example.com", "example.com"));
    REQUIRE(Preferences::DomainMatches("news.example.com", "example.com"));
    REQUIRE(!Preferences::DomainMatches("badexample.com", "example.com"));
    REQUIRE(!Preferences::DomainMatches("example.com.evil.net", "example.com"));
    Preferences p;
    p.trustedImageDomains = { "anthropic.com" };
    REQUIRE(p.IsTrustedDomain("https://claude.ai.anthropic.com/i.png"));
    REQUIRE(p.IsTrustedDomain("mail.anthropic.com"));
    REQUIRE(!p.IsTrustedDomain("https://anthropic.com.example/i.png"));
}

TEST(preferences_round_trip) {
    const std::string path =
        (std::filesystem::temp_directory_path() / "ultramail_prefs_test.ini").string();
    Preferences out;
    out.showReadingPane = false;
    out.fetchSenderIcons = false;
    out.remoteImages = RemoteImagePolicy::LoadNever;
    out.trustedImageDomains = { "anthropic.com", "example.org" };
    out.remoteImageSenders = { "news@example.com" };
    out.showHtml = false;
    out.messageTextSize = 16;
    out.linkDisplay = LinkDisplay::Tooltip;
    out.needsAnswerMaxAgeDays = 30;
    out.needsAnswerOnlyWrittenTo = false;
    out.listSort.key = MessageSortKey::Subject;
    out.listSort.ascending = true;
    out.checkMailEverySec = 40;
    out.notifyNewMail = false;
    out.scamWarnings.romance = false;
    out.scamWarnings.cryptoCaution = false;
    out.senderLists.trusted = { "friend@example.org" };
    out.senderLists.blocked = { "offers@shop.example", "@junk.example" };
    out.folderTreeContent = FolderTreeContent::CurrentAccount;
    out.accountOrder = { "work", "erika", "club" };
    REQUIRE(out.Save(path));

    Preferences in;
    REQUIRE(in.Load(path));
    REQUIRE(!in.showReadingPane);
    REQUIRE(!in.fetchSenderIcons);
    REQUIRE(in.remoteImages == RemoteImagePolicy::LoadNever);
    REQUIRE(in.trustedImageDomains == out.trustedImageDomains);
    REQUIRE(in.remoteImageSenders == out.remoteImageSenders);
    REQUIRE(!in.showHtml);
    REQUIRE_EQ(in.messageTextSize, 16);
    REQUIRE(in.linkDisplay == LinkDisplay::Tooltip);
    REQUIRE(!in.scamWarnings.romance);
    REQUIRE(!in.scamWarnings.cryptoCaution);
    REQUIRE(in.scamWarnings.phishing);          // the others stay on
    REQUIRE(in.scamWarnings.advanceFee);
    REQUIRE(in.scamWarnings == out.scamWarnings);
    REQUIRE(in.senderLists == out.senderLists);
    REQUIRE_EQ(in.needsAnswerMaxAgeDays, 30);
    REQUIRE(!in.needsAnswerOnlyWrittenTo);
    REQUIRE(in.listSort == out.listSort);
    REQUIRE_EQ(in.checkMailEverySec, 40);
    REQUIRE(!in.notifyNewMail);
    REQUIRE(in.folderTreeContent == FolderTreeContent::CurrentAccount);
    REQUIRE(in.accountOrder == out.accountOrder);
    std::remove(path.c_str());
}

TEST(preferences_old_file_keeps_defaults) {
    const std::string path =
        (std::filesystem::temp_directory_path() / "ultramail_prefs_old.ini").string();
    {
        std::ofstream f(UltraCanvas::PathFromUtf8(path));
        f << "reading_pane = true\nfetch_sender_icons = true\nremote_images_from = a@b.c\n"
             "message_text_size = 400\n";
    }
    Preferences in;
    REQUIRE(in.Load(path));
    REQUIRE(in.remoteImages == RemoteImagePolicy::LoadTrusted);   // the default
    REQUIRE(in.showHtml);
    REQUIRE(in.linkDisplay == LinkDisplay::StatusBar);           // the default
    REQUIRE_EQ(in.messageTextSize, 24);                           // clamped
    REQUIRE(in.remoteImageSenders.count("a@b.c") == 1);
    REQUIRE_EQ(in.needsAnswerMaxAgeDays, 14);                     // the defaults
    REQUIRE(in.needsAnswerOnlyWrittenTo);
    REQUIRE(in.listSort == MessageSort{});                        // newest first
    REQUIRE_EQ(in.checkMailEverySec, 300);                        // every 5 minutes
    REQUIRE(in.notifyNewMail);                                    // on until switched off
    REQUIRE(in.folderTreeContent == FolderTreeContent::AllAccounts);
    REQUIRE(in.accountOrder.empty());                             // the store's order
    std::remove(path.c_str());
}

// The sender lists edited by hand: kept as the sender menu would write them,
// and what is no address (or, on the blocked list, no domain) dropped.
TEST(preferences_sender_lists_from_a_hand_edited_file) {
    const std::string path = UltraCanvas::PathToUtf8(
        std::filesystem::temp_directory_path() / "ultramail_prefs_lists.ini");
    {
        std::ofstream f(UltraCanvas::PathFromUtf8(path));
        f << "trusted_senders = Friend@Example.org, @x.example,\n"
             "blocked_senders = junk.example, Spam <SPAM@X.Example>, nonsense\n";
    }
    Preferences in;
    REQUIRE(in.Load(path));
    REQUIRE(in.senderLists.trusted == std::set<std::string>{ "friend@example.org" });
    REQUIRE(in.senderLists.blocked ==
            (std::set<std::string>{ "@junk.example", "spam@x.example" }));
    std::remove(path.c_str());
}

// Settings > Mail > New mail offers ten intervals; a number edited into the
// file by hand comes back as the nearest of them.
TEST(preferences_check_mail_interval_choices) {
    const std::vector<int> expected = { 20, 30, 40, 50, 60, 120, 180, 240, 300, 600 };
    REQUIRE(Preferences::CheckMailChoices() == expected);
    REQUIRE_EQ(Preferences::kDefaultCheckMailSec, 300);

    REQUIRE_EQ(Preferences::NearestCheckMailChoice(20), 20);
    REQUIRE_EQ(Preferences::NearestCheckMailChoice(7), 20);      // below the shortest
    REQUIRE_EQ(Preferences::NearestCheckMailChoice(-5), 20);
    REQUIRE_EQ(Preferences::NearestCheckMailChoice(25), 20);     // a tie: the shorter
    REQUIRE_EQ(Preferences::NearestCheckMailChoice(44), 40);
    REQUIRE_EQ(Preferences::NearestCheckMailChoice(100), 120);
    REQUIRE_EQ(Preferences::NearestCheckMailChoice(3600), 600);  // above the longest

    REQUIRE_EQ(Preferences::CheckMailLabel(20), std::string("20 seconds"));
    REQUIRE_EQ(Preferences::CheckMailLabel(60), std::string("1 minute"));
    REQUIRE_EQ(Preferences::CheckMailLabel(180), std::string("3 minutes"));
    REQUIRE_EQ(Preferences::CheckMailLabel(600), std::string("10 minutes"));

    const std::string path =
        (std::filesystem::temp_directory_path() / "ultramail_prefs_interval.ini").string();
    {
        std::ofstream f(UltraCanvas::PathFromUtf8(path));
        f << "check_mail_every_sec = 75\n";
    }
    Preferences in;
    REQUIRE(in.Load(path));
    REQUIRE_EQ(in.checkMailEverySec, 60);
    {
        std::ofstream f(UltraCanvas::PathFromUtf8(path));
        f << "check_mail_every_sec = often\n";
    }
    Preferences bad;
    REQUIRE(bad.Load(path));
    REQUIRE_EQ(bad.checkMailEverySec, 300);                      // keeps the default
    std::remove(path.c_str());
}

// The account tiles dragged into another order: the accounts follow it, and
// one the order does not name (added since) comes after, in the order it came.
TEST(preferences_order_accounts_by_the_dragged_order) {
    auto make = [](std::initializer_list<const char*> ids) {
        std::vector<Account> accounts;
        for (const char* id : ids) { Account a; a.accountId = id; accounts.push_back(a); }
        return accounts;
    };
    auto ids = [](const std::vector<Account>& accounts) {
        std::vector<std::string> out;
        for (const auto& a : accounts) out.push_back(a.accountId);
        return out;
    };
    Preferences prefs;
    std::vector<Account> accounts = make({"club", "erika", "new", "work"});
    prefs.OrderAccounts(accounts);   // no order yet: as they came
    REQUIRE(ids(accounts) == (std::vector<std::string>{"club", "erika", "new", "work"}));

    prefs.accountOrder = {"work", "gone", "erika", "club"};
    prefs.OrderAccounts(accounts);
    REQUIRE(ids(accounts) == (std::vector<std::string>{"work", "erika", "club", "new"}));

    // Read from a hand-edited file: spaces and repeats do not matter.
    const std::string path =
        (std::filesystem::temp_directory_path() / "ultramail_prefs_order.ini").string();
    {
        std::ofstream f(UltraCanvas::PathFromUtf8(path));
        f << "account_order =  work ,erika,, work\nfolder_tree_content = current\n";
    }
    Preferences in;
    REQUIRE(in.Load(path));
    REQUIRE(in.accountOrder == (std::vector<std::string>{"work", "erika"}));
    REQUIRE(in.folderTreeContent == FolderTreeContent::CurrentAccount);
    std::remove(path.c_str());
}
