// Apps/UltraMail/ui/UltraMailMessagePreview.cpp
// Version: 0.2.0
// Last Modified: 2026-09-03
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailMessagePreview.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasTextArea.h"
#include "HTMLReader/HTMLElementBuilder.h"

#include "UltraMailMimeCodec.h"

#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace UltraCanvas;

namespace UltraMail {

namespace {

constexpr float kSubjectFont = 17.0f;
constexpr float kHeaderFont  = 13.0f;
constexpr float kLineHeight  = 20.0f;
const Color kMutedText(90, 90, 90, 255);

// Very small HTML-to-text reduction (for the quoted reply body): drop tags and
// decode a few entities.
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

std::string JoinAddresses(const std::vector<std::string>& v) {
    std::string s;
    for (std::size_t i = 0; i < v.size(); ++i) { if (i) s += ", "; s += v[i]; }
    return s;
}

std::shared_ptr<UltraCanvasLabel> HeaderLine(const std::string& id) {
    auto label = CreateLabel(id, 0, 0, 0, kLineHeight, "");
    label->SetFontSize(kHeaderFont);
    label->SetTextColor(kMutedText);
    return label;
}

} // namespace

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

std::shared_ptr<UltraCanvasContainer> MessagePreview::Build() {
    root_ = CreateContainer("messagePreview", 0, 0, 0, 0);
    root_->layout.SetFlexColumn()
                 .SetFlexGap(4)
                 .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    subject_ = CreateLabel("prevSubject", 0, 0, 0, 26, "Select a message");
    subject_->SetFontSize(kSubjectFont);
    subject_->SetFontWeight(FontWeight::Bold);
    root_->AddChild(subject_);

    from_ = HeaderLine("prevFrom");
    to_   = HeaderLine("prevTo");
    date_ = HeaderLine("prevDate");
    root_->AddChild(from_);
    root_->AddChild(to_);
    root_->AddChild(date_);

    // Action row.
    auto actions = CreateContainer("prevActions", 0, 0, 0, 34);
    actions->layout.SetFlexRow()
                   .SetFlexGap(8)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    auto replyBtn = CreateButton("prevReply", 0, 0, 100, 28, "↩ Reply");
    replyBtn->onClick = [this]() {
        if (!onReply || !hasMessage_) return;
        std::string selfName, selfAddr;
        for (const auto& a : accounts_)
            if (a.accountId == curAccount_) { selfName = a.displayName; selfAddr = a.email; }
        onReply(current_, selfName, selfAddr);
    };
    actions->AddChild(replyBtn);
    root_->AddChild(actions);

    // Body host: takes the remaining height; RenderBody() fills it with either
    // a read-only text area (plain text) or the HTMLReader-built element tree.
    bodyHost_ = CreateContainer("prevBodyHost", 0, 0, 0, 0);
    bodyHost_->layout.SetFlexColumn()
                     .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    root_->AddChild(bodyHost_);
    bodyHost_->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    // Attachment chips below the body (fixed height, hidden while empty).
    auto strip = attachmentStrip_.Build();
    root_->AddChild(strip);
    attachmentStrip_.onOpen = [this](const Attachment& a) {
        if (onOpenAttachment) onOpenAttachment(a);
    };

    Clear();
    return root_;
}

void MessagePreview::RenderBody(const std::string& body, bool isHtml) {
    if (!bodyHost_) return;
    bodyHost_->ClearChildren();

    const float w = bodyHost_->GetWidth();
    const float h = bodyHost_->GetHeight();

    if (isHtml && w > 0 && h > 0) {
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

    // The text area is sized by the host's flex column, so it follows the pane.
    auto text = std::make_shared<UltraCanvasTextArea>("prevBodyText", 0, 0, 0, 0);
    text->SetReadOnly(true);
    text->SetEditingMode(TextAreaEditingMode::PlainText);
    text->SetText(isHtml ? HtmlToText(body) : body);
    bodyHost_->AddChild(text);
    text->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
}

void MessagePreview::Clear() {
    hasMessage_ = false;
    current_ = SourceMessage{};
    if (subject_) subject_->SetText("Select a message");
    if (from_)    from_->SetText("");
    if (to_)      to_->SetText("");
    if (date_)    date_->SetText("");
    if (bodyHost_) bodyHost_->ClearChildren();
    attachmentStrip_.SetAttachments({});
}

void MessagePreview::Show(const MessageEnvelope& env) {
    hasMessage_ = true;
    curAccount_ = env.accountId;

    std::string sender = env.fromName.empty()
        ? env.fromAddr : (env.fromName + " <" + env.fromAddr + ">");
    if (subject_) subject_->SetText(env.subject.empty() ? "(no subject)" : env.subject);
    if (from_)    from_->SetText("From: " + sender);
    if (to_)      to_->SetText("To: " + JoinAddresses(env.to));
    if (date_)    date_->SetText("Date: " + FormatShortDate(env.date));

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
        RenderBody("(message body not downloaded yet)", false);
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
