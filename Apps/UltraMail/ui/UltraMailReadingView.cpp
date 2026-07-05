// Apps/UltraMail/ui/UltraMailReadingView.cpp
// Version: 0.1.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailReadingView.h"

#include "UltraMailMimeCodec.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasEvent.h"

#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace UltraCanvas;

namespace UltraMail {

namespace {

constexpr float kFolderW = 190.0f;
constexpr float kListW   = 360.0f;
constexpr float kRowH    = 48.0f;

std::string FormatShortDate(int64_t epoch) {
    if (epoch <= 0) return "";
    std::time_t t = static_cast<std::time_t>(epoch);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%b %d, %Y %H:%M", &tm);
    return buf;
}

// Very small HTML-to-text reduction for the preview (a full render goes through
// HTMLReader later): drop tags and decode a few entities.
std::string HtmlToText(const std::string& html) {
    std::string out;
    bool inTag = false;
    for (std::size_t i = 0; i < html.size(); ++i) {
        char c = html[i];
        if (c == '<') { inTag = true; continue; }
        if (c == '>') { inTag = false; out.push_back(' '); continue; }
        if (inTag) continue;
        if (c == '&') {
            if (html.compare(i, 5, "&amp;") == 0) { out.push_back('&'); i += 4; continue; }
            if (html.compare(i, 4, "&lt;") == 0)  { out.push_back('<'); i += 3; continue; }
            if (html.compare(i, 4, "&gt;") == 0)  { out.push_back('>'); i += 3; continue; }
            if (html.compare(i, 6, "&nbsp;") == 0){ out.push_back(' '); i += 5; continue; }
        }
        out.push_back(c);
    }
    return out;
}

std::string SanitizeFolder(const std::string& folder) {
    std::string out;
    for (char c : folder) out.push_back((c == '/' || c == '\\' || c == ':') ? '_' : c);
    return out.empty() ? "INBOX" : out;
}

} // namespace

// ---- MessageRow ------------------------------------------------------------

MessageRow::MessageRow(const std::string& id, float x, float y, float w, float h,
                       const MessageEnvelope& env,
                       std::function<void()> onSelect, std::function<void()> onOpen)
    : UltraCanvasContainer(id, x, y, w, h),
      onSelect_(std::move(onSelect)), onOpen_(std::move(onOpen)) {
    const bool unread = (env.flags & Flag_Seen) == 0;
    AddChild(CreateLabel(id + ".dot", 6, 4, 16, 18, unread ? "●" : ""));
    std::string sender = env.fromName.empty() ? env.fromAddr : env.fromName;
    AddChild(CreateLabel(id + ".from", 24, 4, w - 30, 18, sender));
    AddChild(CreateLabel(id + ".subj", 24, 24, w - 30, 18,
                         env.subject.empty() ? "(no subject)" : env.subject));
    AddChild(CreateLabel(id + ".date", w - 130, 4, 124, 16, FormatShortDate(env.date)));
}

bool MessageRow::OnEvent(const UCEvent& event) {
    if (!IsVisible() || IsDisabled()) return false;
    if (event.type == UCEventType::MouseDoubleClick) { if (onOpen_) onOpen_(); return true; }
    if (event.type == UCEventType::MouseDown && event.button == UCMouseButton::Left) {
        if (onSelect_) onSelect_();
        return true;
    }
    return UltraCanvasContainer::OnEvent(event);
}

// ---- ReadingView -----------------------------------------------------------

std::shared_ptr<UltraCanvasContainer> ReadingView::Build() {
    root_ = CreateContainer("readingView", 0, 0, 0, 0);

    folderPane_ = CreateContainer("readFolders", 0, 0, kFolderW, 0);
    root_->AddChild(folderPane_);

    listPane_ = CreateContainer("readList", kFolderW, 0, kListW, 0);
    root_->AddChild(listPane_);

    const float px = kFolderW + kListW;
    previewPane_ = CreateContainer("readPreview", px, 0, 560, 0);

    // Child offsets are relative to previewPane_ (which is already at x = px).
    hdrFrom_    = CreateLabel("prevFrom", 12, 12, 520, 20, "");
    hdrSubject_ = CreateLabel("prevSubject", 12, 34, 520, 22, "Select a message");
    hdrDate_    = CreateLabel("prevDate", 12, 58, 520, 18, "");
    previewPane_->AddChild(hdrFrom_);
    previewPane_->AddChild(hdrSubject_);
    previewPane_->AddChild(hdrDate_);

    auto replyBtn = CreateButton("prevReply", 12, 84, 100, 28, "↩ Reply");
    replyBtn->onClick = [this]() {
        if (!onReply) return;
        std::string selfName, selfAddr;
        for (const auto& a : accounts_)
            if (a.accountId == curAccount_) { selfName = a.displayName; selfAddr = a.email; }
        onReply(current_, selfName, selfAddr);
    };
    previewPane_->AddChild(replyBtn);

    // Body host: a fixed-size container that RenderBody() fills with either a
    // read-only text area (plain text) or the HTMLReader-built element tree.
    bodyHost_ = CreateContainer("prevBodyHost", 12, 120, 540, 300);
    previewPane_->AddChild(bodyHost_);

    // The attachment strip's own container sits at (0,0) of its parent, so wrap
    // it in a positioned container to place it below the body.
    auto attWrap = CreateContainer("prevAttWrap", 12, 430, 560, 64);
    attWrap->AddChild(attachmentStrip_.Build());
    previewPane_->AddChild(attWrap);
    attachmentStrip_.onOpen = [this](const Attachment& a) { if (onOpenAttachment) onOpenAttachment(a); };

    root_->AddChild(previewPane_);

    RebuildFolders();
    // Default to the first account's inbox.
    if (!accounts_.empty()) SelectFolder(accounts_.front().accountId, "INBOX");
    return root_;
}

void ReadingView::RebuildFolders() {
    if (!folderPane_ || !store_) return;
    folderPane_->ClearChildren();
    folderPane_->AddChild(CreateLabel("readFoldersHdr", 12, 12, kFolderW - 20, 22, "Mailboxes"));

    float y = 42;
    for (const auto& acc : accounts_) {
        folderPane_->AddChild(CreateLabel("acc_" + acc.accountId, 8, y, kFolderW - 16, 22,
                                          acc.shortName));
        y += 26;
        std::vector<Folder> folders;
        store_->ListFolders(acc.accountId, folders);
        for (const auto& f : folders) {
            const std::string aid = acc.accountId, fname = f.name;
            std::string marker = (aid == curAccount_ && fname == curFolder_) ? "▸ " : "   ";
            auto lbl = CreateLabel("fld_" + aid + "_" + fname, 20, y, kFolderW - 28, 20,
                                   marker + f.name);
            lbl->onClick = [this, aid, fname]() { SelectFolder(aid, fname); };
            folderPane_->AddChild(lbl);
            y += 24;
        }
        y += 8;
    }
}

void ReadingView::SelectFolder(const std::string& accountId, const std::string& folder) {
    curAccount_ = accountId;
    curFolder_ = folder;
    RebuildFolders();
    RebuildList();
}

void ReadingView::RebuildList() {
    if (!listPane_ || !store_) return;
    listPane_->ClearChildren();

    std::vector<MessageEnvelope> msgs;
    store_->ListMessages(curAccount_, curFolder_, 0, msgs);
    if (msgs.empty()) {
        listPane_->AddChild(CreateLabel("readEmpty", 12, 12, kListW - 20, 20,
                                        "No messages in this folder."));
        return;
    }
    float y = 0;
    for (const auto& m : msgs) {
        MessageEnvelope copy = m;
        auto onSelect = [this, copy]() { SelectMessage(copy); };
        auto onOpen   = [this, copy]() { SelectMessage(copy); };  // (own-window view later)
        auto row = std::make_shared<MessageRow>(
            "msg_" + std::to_string(m.uid), 0, y, kListW - 4, kRowH, m,
            std::move(onSelect), std::move(onOpen));
        listPane_->AddChild(row);
        y += kRowH + 2;
    }

    // Show the most recent message's preview by default.
    SelectMessage(msgs.front());
}

void ReadingView::RenderBody(const std::string& body, bool isHtml) {
    if (!bodyHost_) return;
    bodyHost_->ClearChildren();

    const float w = bodyHost_->GetWidth();
    const float h = bodyHost_->GetHeight();

    if (isHtml) {
        // Full render through the HTMLReader element builder: the CSSLayout
        // engine measures and lays out a native UltraCanvas tree (containers +
        // Pango-markup labels + images).
        HTML::BuildOptions opts;
        opts.style.baseFontSizePx = 14.0f;
        opts.enableImages = true;
        // No remote fetch in the preview: images resolve to empty (placeholder).
        opts.resourceLoader = [](const std::string&) { return std::vector<uint8_t>{}; };
        HTML::ElementBuilder builder;
        HTML::BuildResult r = builder.Build(body, opts);
        if (r.root) {
            r.root->SetPosition(0, 0);
            r.root->SetSize(w, h);
            bodyHost_->AddChild(r.root);
            return;
        }
        // Fall through to a text area if the build produced nothing.
    }

    auto text = std::make_shared<UltraCanvasTextArea>("prevBodyText", 0, 0, w, h);
    text->SetReadOnly(true);
    text->SetEditingMode(TextAreaEditingMode::PlainText);
    text->SetText(isHtml ? HtmlToText(body) : body);
    bodyHost_->AddChild(text);
}

void ReadingView::SelectMessage(const MessageEnvelope& env) {
    std::string sender = env.fromName.empty()
        ? env.fromAddr : (env.fromName + " <" + env.fromAddr + ">");
    if (hdrFrom_)    hdrFrom_->SetText("From: " + sender);
    if (hdrSubject_) hdrSubject_->SetText(env.subject.empty() ? "(no subject)" : env.subject);
    if (hdrDate_)    hdrDate_->SetText(FormatShortDate(env.date));

    // Load the cached body (.eml) and decode it.
    fs::path path = fs::path(mailDir_) / env.accountId / SanitizeFolder(env.folder)
                  / (std::to_string(env.uid) + ".eml");
    std::error_code ec;
    if (fs::exists(path, ec)) {
        std::ifstream is(path, std::ios::binary);
        std::string raw((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());
        ParsedMessage pm = MimeCodec::Parse(raw);
        RenderBody(pm.body, pm.bodyIsHtml);
        attachmentStrip_.SetAttachments(pm.attachments);
        // Reply quoting works from text; reduce HTML to text for the captured copy.
        current_.body = pm.bodyIsHtml ? HtmlToText(pm.body) : pm.body;
        current_.attachments = pm.attachments;
    } else {
        RenderBody("(message body not downloaded)", false);
        attachmentStrip_.SetAttachments({});
        current_.body.clear();
        current_.attachments.clear();
    }

    // Capture the selection for a possible Reply.
    current_.messageId = env.messageId;
    current_.fromName  = env.fromName;
    current_.fromAddr  = env.fromAddr;
    current_.to        = env.to;
    current_.subject   = env.subject;
    current_.date      = FormatShortDate(env.date);
}

} // namespace UltraMail
