// Apps/UltraMail/ui/UltraMailApp.cpp
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailApp.h"

#include "UltraMailAttachmentCache.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasMediaViewer.h"

#include <UltraNet/UltraNetMime.h>

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <string>

using namespace UltraCanvas;

namespace UltraMail {

std::string UltraMailApp::LocalPart(const std::string& email) {
    auto at = email.find('@');
    return at == std::string::npos ? email : email.substr(0, at);
}

std::string UltraMailApp::SlugFromEmail(const std::string& email) {
    std::string slug;
    for (char c : email) {
        if (std::isalnum(static_cast<unsigned char>(c)))
            slug.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        else
            slug.push_back('-');
    }
    return slug;
}

bool UltraMailApp::Initialize(const std::string& dataDir) {
    std::error_code ec;
    std::filesystem::create_directories(dataDir, ec);
    const std::string dbPath = dataDir + "/mail.db";

    UltraDbResult opened = store_.Open("ultramail", dbPath);
    if (!opened) return false;

    cacheDir_ = dataDir + "/cache";

    // The address book is a global (account-independent) store.
    contacts_.Open("ultramail-contacts", dataDir + "/contacts.db");

    store_.ListAccounts(accounts_);
    store_.GetAccountStatus(status_);
    return true;
}

std::shared_ptr<UltraCanvasWindow> UltraMailApp::CreateMainWindow() {
    WindowConfig config;
    config.title  = "UltraMail";
    config.width  = 960;
    config.height = 640;
    window_ = CreateWindow(config);

    // Header line.
    window_->AddChild(CreateLabel("umTitle", 16, 8, 400, 28, "UltraMail"));

    // Account info-tile bar (populated once accounts exist).
    auto bar = infoBar_.Build();
    window_->AddChild(bar);
    infoBar_.onAccountClicked     = [](const std::string&) { /* Phase 2: open account */ };
    infoBar_.onNeedsAnswerClicked = [](const std::string&) { /* Phase 2: needs-answer view */ };

    // Toolbox grid of account tiles + the "Add email account" tile.
    auto grid = toolbox_.Build();
    window_->AddChild(grid);
    toolbox_.onAddAccount  = [this]() { HandleAddAccount(); };
    toolbox_.onOpenAccount = [](const std::string&) { /* Phase 2: open the 3-pane view */ };

    // Contacts entry point.
    auto contactsBtn = CreateButton("umContacts", 640, 8, 120, 28, "Contacts");
    contactsBtn->onClick = [this]() { OpenContacts(); };
    window_->AddChild(contactsBtn);

    // Attachment strip (populated when a message with attachments is shown).
    auto strip = attachmentStrip_.Build();
    window_->AddChild(strip);
    attachmentStrip_.onOpen   = [this](const Attachment& a) { OpenAttachment(a); };
    attachmentStrip_.onSaveAs = [this](const Attachment& a) { SaveAttachment(a); };

    // First-run hint below the grid.
    window_->AddChild(CreateLabel("umHint", 16, 600, 600, 24,
        "Welcome to UltraMail. Add an account to begin."));

    Refresh();

    // Demo path: exercise the attachment strip + viewer without a live sync.
    if (const char* demo = std::getenv("ULTRAMAIL_DEMO"); demo && *demo == '1')
        ShowDemoAttachments();
    // Demo path: seed contacts and open the contact manager.
    if (const char* dc = std::getenv("ULTRAMAIL_DEMO_CONTACTS"); dc && *dc == '1') {
        SeedDemoContacts();
        OpenContacts();
    }

    return window_;
}

void UltraMailApp::OpenContacts() {
    if (!contacts_.IsOpen()) return;
    WindowConfig cfg;
    cfg.title  = "Contacts";
    cfg.width  = 640;
    cfg.height = 520;
    auto win = CreateWindow(cfg);

    contactsView_.SetStore(&contacts_);
    win->AddChild(contactsView_.Build());
    win->Show();
    viewerWindows_.push_back(win);
}

void UltraMailApp::SeedDemoContacts() {
    std::vector<SectionCount> counts;
    if (contacts_.GetSectionCounts(counts)) {
        int total = 0;
        for (auto& c : counts) total += c.count;
        if (total > 0) return;   // already seeded
    }
    auto add = [&](const std::string& name, ContactSection section,
                   const std::string& email, const std::string& phone,
                   const std::string& org) {
        Contact c; c.displayName = name; c.section = section; c.organization = org;
        if (!email.empty()) { ContactEmail e; e.address = email; e.primary = true; c.emails.push_back(e); }
        if (!phone.empty()) { ContactPhone p; p.number = phone; p.label = "mobile"; c.phones.push_back(p); }
        contacts_.Save(c);
    };
    add("Mum",          ContactSection::Family,   "mum@example.com",   "+49 170 1112222", "");
    add("Brother Tom",  ContactSection::Family,   "tom@example.com",   "+49 151 3334444", "");
    add("Anna Schmidt", ContactSection::Friends,  "anna@example.com",  "+49 160 5556666", "");
    add("Max Weber",    ContactSection::Friends,  "max@example.com",   "",                "");
    add("Carol Boss",   ContactSection::Work,     "carol@acme.com",    "+49 30 1234567",  "Acme GmbH");
    add("IT Helpdesk",  ContactSection::Work,     "help@acme.com",     "",                "Acme GmbH");
    add("Chess Club",   ContactSection::Leisure,  "info@chessclub.org","",                "");
    add("Plumber",      ContactSection::Services, "service@plumb.example", "+49 40 7654321", "Plumb & Co");
    add("Electricity",  ContactSection::Services, "billing@power.example", "",             "PowerCo");
}

void UltraMailApp::ShowAttachments(const ParsedMessage& message) {
    currentMessage_ = message;
    attachmentStrip_.SetAttachments(currentMessage_.attachments);
}

void UltraMailApp::OpenAttachment(const Attachment& attachment) {
    AttachmentCache cache(cacheDir_);
    const std::string path = cache.Write(attachment);
    if (path.empty()) return;

    WindowConfig cfg;
    cfg.title  = attachment.filename.empty() ? "Attachment" : attachment.filename;
    cfg.width  = 900;
    cfg.height = 680;
    auto win = CreateWindow(cfg);

    auto viewer = CreateMediaViewer("attachmentViewer", 0, 0,
                                    static_cast<float>(cfg.width),
                                    static_cast<float>(cfg.height));
    win->AddChild(viewer);
    viewer->OpenFile(path);
    win->Show();

    viewerWindows_.push_back(win);   // keep the window alive
}

void UltraMailApp::SaveAttachment(const Attachment& attachment) {
    // Phase 2: persist into the cache directory. A native Save-As dialog
    // (UltraCanvasFileLoader) replaces this once wired.
    AttachmentCache cache(cacheDir_);
    cache.Write(attachment);
}

void UltraMailApp::ShowDemoAttachments() {
    // Build a small multipart message with a text attachment, then run it
    // through the same MIME codec a fetched message would use.
    UltraNetMimeBuildInput in;
    in.from = "demo@ultramail.local";
    in.to = {"you@ultramail.local"};
    in.subject = "Demo message with an attachment";
    in.body = "This message carries an attachment — double-click it below "
              "or right-click for Open / Save As.";
    in.date = "Tue, 01 Jan 2026 00:00:00 +0000";
    in.messageId = "<demo@ultramail.local>";

    const std::string note =
        "UltraMail attachment demo\r\n\r\n"
        "This text file was extracted from the email's MIME parts by the "
        "UltraNet MIME codec, written to the attachment cache, and opened in "
        "UltraCanvasMediaViewer.\r\n";
    UltraNetMimeBuildAttachment a;
    a.filename = "readme.txt";
    a.mediaType = "text/plain";
    a.data.assign(note.begin(), note.end());
    in.attachments.push_back(a);

    ParsedMessage parsed = MimeCodec::Parse(UltraNet_MimeBuild(in));
    ShowAttachments(parsed);

    if (const char* open = std::getenv("ULTRAMAIL_DEMO_OPEN");
        open && *open == '1' && !parsed.attachments.empty())
        OpenAttachment(parsed.attachments.front());
}

void UltraMailApp::Refresh() {
    store_.ListAccounts(accounts_);
    store_.GetAccountStatus(status_);
    infoBar_.Rebuild(status_);
    toolbox_.Rebuild(accounts_, status_);
}

void UltraMailApp::HandleAddAccount() {
    AccountWizard::Show(window_.get(),
        [this](const AccountDraft& draft) { HandleWizardSubmit(draft); });
}

void UltraMailApp::HandleWizardSubmit(const AccountDraft& draft) {
    // Phase 1: store the account locally so its tiles appear. The discovery +
    // login-verify pipeline (provider presets -> autoconfig -> DNS SRV ->
    // probing, over UltraNet) and the credential vault land in a later slice;
    // the password is intentionally not persisted here.
    Account a;
    a.accountId   = SlugFromEmail(draft.email);
    a.email       = draft.email;
    a.displayName = draft.displayName.empty() ? LocalPart(draft.email) : draft.displayName;
    a.shortName   = LocalPart(draft.email);

    if (!store_.UpsertAccount(a)) return;

    // Give the account an inbox so the Toolbox tile and rollups have somewhere
    // to hang; real folders arrive from the first IMAP sync.
    Folder inbox;
    inbox.accountId = a.accountId;
    inbox.name      = "INBOX";
    inbox.role      = FolderRole::Inbox;
    store_.UpsertFolder(inbox);

    Refresh();
}

} // namespace UltraMail
