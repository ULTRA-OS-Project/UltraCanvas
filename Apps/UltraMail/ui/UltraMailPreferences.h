// Apps/UltraMail/ui/UltraMailPreferences.h
// App-wide UltraMail preferences (view options that are remembered between
// runs, independent of any account). Stored as a small key=value file next to
// the other per-user files under the data directory (preferences.ini), the
// same way oauth.ini lives there. Not per-account server settings — those stay
// on the Account in the local store.
// Version: 0.10.0 - notifyNewMail: a notification on screen when new mail arrives
//                   (Settings > Display > Notifications)
// Version: 0.9.0 - fetchSiteIcons (the website icon of a sender that is no
//                  known service)
// Version: 0.8.0 - how often new mail is checked (check_mail_every_sec, Settings >
//                  Mail > New mail)
// Version: 0.7.0 - the message list's order (list_sort), chosen in its column headers
// Version: 0.6.0 - waiting-for-reply rules (its age, only people written to)
// Version: 0.5.0 - link display: the status bar or a tooltip (Settings > Display > Links)
// Version: 0.4.0 - folder tree width: fixed pixels or fitted to the names
// Version: 0.3.0 - remote-image policy, trusted websites, message view and text size
//                  (the Settings window, UltraMailSettingsDialog)
// Version: 0.2.0 - fetchSenderIcons (download the known senders' icons)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailMessageSort.h"

#include <set>
#include <string>
#include <vector>

namespace UltraMail {

// When the pictures a message links to on the web are downloaded. Loading one
// tells its sender that - and when - the message was opened.
// (Load- prefixed: X11's headers #define Always.)
enum class RemoteImagePolicy {
    LoadAlways,    // every message (except junk and suspicious mail)
    LoadTrusted,   // trusted senders, trusted websites and the address book
    LoadNever      // never by themselves: the "Show images" bar asks each time
};

// How wide the folder tree on the left of the mail view is.
enum class FolderTreeWidthMode {
    FitToText,   // as wide as its longest row needs, plus 10 px
    FixedWidth   // folderTreeWidth pixels
};

// Where the address behind a link in a message is shown.
enum class LinkDisplay {
    StatusBar,   // the status bar lists the message's links and shows the hovered one
    Tooltip      // a tooltip over the hovered link; the status bar stays quiet
};

// The handful of app-wide view options. Add fields here (with a default) and a
// matching key in Load/Save; unknown keys and a missing file are ignored so an
// older/newer file never breaks startup.
struct Preferences {
    // Show the message preview (reading) pane beside the list. When false the
    // list takes the whole content area and a clicked message opens in its
    // place (Gmail-style).
    bool showReadingPane = true;

    // Whether UltraMail may download the icons of the services in its known-
    // sender registry into the sender-icon cache, once each. Off means the
    // badge shows the sender's monogram in the brand's colour instead - and
    // nothing at all is downloaded, website icons included.
    bool fetchSenderIcons = true;
    // Whether a sender that is no known service gets its website's icon:
    // the home page of the domain it writes from is read for its icon, once
    // a week at most, and only for mail that passed the content scan. That
    // tells the sender's web server that someone looked; off, only the
    // registry's icons are fetched.
    bool fetchSiteIcons = true;

    // Senders whose remote (web) images load without asking ("Always from
    // <sender>" in the reading pane), lower-cased addresses.
    std::set<std::string> remoteImageSenders;

    // Settings > Privacy > Images.
    RemoteImagePolicy remoteImages = RemoteImagePolicy::LoadTrusted;
    // Trusted websites (domains, lower case, "example.com"): a message from
    // that domain or one of its subdomains loads its pictures, and a picture
    // hosted there loads in any message - under the Trusted policy.
    std::set<std::string> trustedImageDomains;

    // Settings > Reading > Messages: show HTML mail formatted (false: as plain
    // text - no layout, no pictures), and the body text size in CSS px.
    bool  showHtml = true;
    int   messageTextSize = 12;

    // Settings > Reading > Layout: the folder tree's width - fitted to the
    // folder and account names it shows, or a fixed number of pixels
    // (kFolderTreeMinWidth..kFolderTreeMaxWidth).
    static constexpr int kFolderTreeMinWidth     = 100;
    static constexpr int kFolderTreeMaxWidth     = 600;
    static constexpr int kFolderTreeDefaultWidth = 200;
    FolderTreeWidthMode folderTreeWidthMode = FolderTreeWidthMode::FitToText;
    int                 folderTreeWidth     = kFolderTreeDefaultWidth;

    // Settings > Display > Links: where a link's address is shown.
    LinkDisplay linkDisplay = LinkDisplay::StatusBar;
    // Settings > Reading > Waiting for reply: which unanswered mail sent to
    // the reader counts as waiting (the account bar's third number, the ↩ in
    // the list, "Needs an answer"). Only mail from the last this-many days
    // (0 = any age), and only from people the reader has written to.
    int  needsAnswerMaxAgeDays   = 14;
    bool needsAnswerOnlyWrittenTo = true;

    // The message list's order: the column header last clicked, and which
    // way round. Newest first until one is clicked.
    MessageSort listSort;

    // Settings > Mail > New mail: how often every account is checked for new
    // mail, in seconds - one of CheckMailChoices().
    static constexpr int kDefaultCheckMailSec = 300;
    int checkMailEverySec = kDefaultCheckMailSec;
    // The intervals offered: 20, 30, 40 and 50 seconds, 1 to 5 minutes, 10
    // minutes - shortest first.
    static const std::vector<int>& CheckMailChoices();
    // The offered interval nearest to `seconds`: a file edited by hand may
    // hold any number, and the dropdown shows only the choices.
    static int NearestCheckMailChoice(int seconds);
    // "20 seconds", "1 minute", "10 minutes".
    static std::string CheckMailLabel(int seconds);

    // Settings > Display > Notifications: when a sync brings new mail into an
    // inbox, a notification on screen - posted through UltraMessage, drawn by
    // the desktop's notification server - names the sender and subject (or
    // counts the messages), and a click on it opens the mail.
    bool notifyNewMail = true;

    // "anthropic.com" from "https://www.Anthropic.com/x", "@anthropic.com" or
    // "*.anthropic.com"; empty when nothing like a domain is left.
    static std::string NormalizeDomain(const std::string& text);
    // Whether `host` is `domain` or one of its subdomains (both lower case).
    static bool DomainMatches(const std::string& host, const std::string& domain);
    // Whether `host` matches one of the trusted websites.
    bool IsTrustedDomain(const std::string& host) const;

    // Read `path`; missing file or keys keep the defaults. Returns false only
    // when the file exists but could not be opened.
    bool Load(const std::string& path);

    // Write every value to `path` (created/truncated). Returns false on an I/O
    // error.
    bool Save(const std::string& path) const;
};

} // namespace UltraMail
