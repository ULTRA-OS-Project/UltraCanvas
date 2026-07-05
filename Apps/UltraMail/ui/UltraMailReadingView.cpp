// Apps/UltraMail/ui/UltraMailReadingView.cpp
// Version: 0.1.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailReadingView.h"

#include "UltraMailMimeCodec.h"

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

    body_ = std::make_shared<UltraCanvasTextArea>("prevBody", 12, 88, 540, 330);
    body_->SetReadOnly(true);
    body_->SetEditingMode(TextAreaEditingMode::PlainText);
    previewPane_->AddChild(body_);

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
        std::string text = pm.bodyIsHtml ? HtmlToText(pm.body) : pm.body;
        if (body_) body_->SetText(text);
        attachmentStrip_.SetAttachments(pm.attachments);
    } else {
        if (body_) body_->SetText("(message body not downloaded)");
        attachmentStrip_.SetAttachments({});
    }
}

} // namespace UltraMail
