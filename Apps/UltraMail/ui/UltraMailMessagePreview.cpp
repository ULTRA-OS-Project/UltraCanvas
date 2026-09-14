// Apps/UltraMail/ui/UltraMailMessagePreview.cpp
// Version: 0.4.3 - From/To are auto-height labels (never cropped); the HTML body
//                  fills the pane width (reflows) and gets a horizontal scrollbar
//                  when content cannot reflow, instead of being clipped.
// Last Modified: 2026-09-13
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailMessagePreview.h"

#include "UltraCanvasConfig.h"
#include "UltraCanvasFileLoader.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasTextArea.h"
#include "HTMLReader/HTMLElementBuilder.h"

#include "UltraMailMimeCodec.h"
#include "UltraMailTheme.h"

#include <UltraNet/UltraNetMime.h>

#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace UltraCanvas;

namespace UltraMail {

namespace {

constexpr float kSubjectFont  = 13.0f;
constexpr float kHeaderLine   = 16.0f;   // line box for the 9pt / 8.5pt header text
constexpr float kAvatarSide   = 26.0f;
constexpr float kDateWidth    = 110.0f;
// The header row holds two stacked auto-height text lines (from / to) plus the
// 1px gap, and must be tall enough for both so neither is cropped.
constexpr float kHeaderHeight = 44.0f;

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

// The sender's initial for the avatar square ("?" for an empty address).
std::string SenderInitial(const std::string& name, const std::string& addr) {
    for (char c : name.empty() ? addr : name)
        if (std::isalnum(static_cast<unsigned char>(c)))
            return std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    return "?";
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
                 .SetFlexGap(Theme::kInnerGap)
                 .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    // Auto-height, word-wrapping subject: a long subject wraps to the pane
    // width and grows downward instead of overflowing horizontally (which drew
    // a scrollbar and painted over the header row). Never shrink it.
    subject_ = Theme::MakeText("prevSubject", "Select a message", kSubjectFont,
                               Theme::kTextPrimary, FontWeight::Bold);
    subject_->SetWrap(TextWrap::WrapWord);
    root_->AddChild(subject_);
    subject_->layoutItem.SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    // Header row: [avatar] from / to ........ date  [Reply]
    header_ = CreateContainer("prevHeader", 0, 0, 0, kHeaderHeight);
    auto& header = header_;
    header->layout.SetFlexRow()
                  .SetFlexGap(10)
                  .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    // Chrome row — never scroll (a long from/to must not fabricate a scrollbar).
    if (auto s = header->GetContainerStyle(); true) {
        s.autoShowScrollbars = false;
        header->SetContainerStyle(s);
    }

    avatarHost_ = CreateContainer("prevAvatarHost", 0, 0, kAvatarSide, kAvatarSide);
    avatarHost_->layout.SetFlexRow();
    header->AddChild(avatarHost_);

    auto who = CreateContainer("prevWho", 0, 0, 0, 0);
    who->layout.SetFlexColumn()
               .SetFlexGap(1)
               .SetFlexJustifyContent(CSSLayout::JustifyContent::Center);
    // Auto-height labels so each sizes to its own glyph line — a fixed-height
    // box cropped the second line regardless of the row height.
    from_ = Theme::MakeText("prevFrom", "", Theme::kSizeBody,
                            Theme::kTextPrimary, FontWeight::Bold);
    to_   = Theme::MakeText("prevTo", "", Theme::kSizeSecondary,
                            Theme::kTextSecondary);
    who->AddChild(from_);
    who->AddChild(to_);
    header->AddChild(who);
    who->layoutItem.SetFlexGrow(1);

    date_ = Theme::MakeLine("prevDate", "", kHeaderLine, Theme::kSizeSecondary,
                            Theme::kTextSecondary);
    date_->SetElementSize(Size2Df(kDateWidth, kHeaderLine));
    date_->SetAlignment(TextAlignment::Right);
    header->AddChild(date_);
    date_->layoutItem.SetFlexShrink(0);

    auto replyBtn = CreateButton("prevReply", 0, 0, 84, 30, "Reply");
    Theme::StyleSecondary(replyBtn);
    replyBtn->SetIcon(NormalizePath(GetResourcesDir() + "media/icons/undo.svg"));
    replyBtn->SetIconPosition(ButtonIconPosition::Left);
    replyBtn->SetIconSize(14, 14);
    replyBtn->SetIconSpacing(6);
    replyBtn->SetUseIconAsMask(true);
    replyBtn->onClick = [this]() {
        if (!onReply || !hasMessage_) return;
        std::string selfName, selfAddr;
        for (const auto& a : accounts_)
            if (a.accountId == curAccount_) { selfName = a.displayName; selfAddr = a.email; }
        onReply(current_, selfName, selfAddr);
    };
    header->AddChild(replyBtn);
    replyBtn->layoutItem.SetFlexShrink(0);
    root_->AddChild(header);
    header->layoutItem.SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    rule_ = Theme::MakeDivider("prevRule");
    root_->AddChild(rule_);

    // Body host: takes the remaining height; RenderBody() fills it with either
    // a read-only text area (plain text) or the HTMLReader-built element tree.
    bodyHost_ = CreateContainer("prevBodyHost", 0, 0, 0, 0);
    bodyHost_->layout.SetFlexColumn()
                     .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    // The body host IS the scroll view for a tall message: keep the vertical
    // scrollbar (auto), but never a horizontal one — HTML reflows to the width,
    // and when the vertical bar appears it must not fabricate horizontal overflow.
    if (auto s = bodyHost_->GetContainerStyle(); true) {
        s.autoShowHorizontalScrollbar = false;
        bodyHost_->SetContainerStyle(s);
    }
    root_->AddChild(bodyHost_);
    bodyHost_->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    // Attachment chips below the body (fixed height, hidden while empty).
    auto strip = attachmentStrip_.Build();
    root_->AddChild(strip);
    attachmentStrip_.onSaveAs = [this](const Attachment& a) {
        if (onSaveAttachment) onSaveAttachment(a);
    };
    attachmentStrip_.onOpen = [this](const Attachment& a) {
        if (onOpenAttachment) onOpenAttachment(a);
    };

    Clear();
    return root_;
}

void MessagePreview::RenderBody(const std::string& body, bool isHtml) {
    if (!bodyHost_) return;
    bodyHost_->ClearChildren();

    if (isHtml) {
        // Full render through the HTMLReader element builder: the CSSLayout
        // engine measures and lays out a native UltraCanvas tree (containers +
        // Pango-markup labels + images).
        HTML::BuildOptions opts;
        opts.style.baseFontSizePx = 12.0f;   // ≈ the 9pt UI font
        opts.enableImages = true;
        // No remote fetch in the preview: images resolve to empty (placeholder).
        opts.resourceLoader = [](const std::string&) { return std::vector<uint8_t>{}; };
        HTML::ElementBuilder builder;
        HTML::BuildResult r = builder.Build(body, opts);
        if (r.root) {
            // Host the tree in a dedicated scroll container (the proven pattern
            // from UltraCanvasEBookViewer): a plain container sized by the flex
            // column, holding r.root with no stretch/size/grow. It clips and
            // scrolls; the tree measures at the container width and takes its
            // natural height — so the body sits below the header (no overlap)
            // and scrolls vertically when tall. The builder disables the tree's
            // own scrollbars precisely so the host scrolls instead.
            auto scroll = CreateContainer("prevBodyScroll", 0, 0, 0, 0);
            scroll->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
            // Keep the vertical auto-scrollbar; also allow a horizontal one so
            // content that genuinely cannot reflow (fixed-width tables, large
            // images) can be scrolled to instead of being clipped.
            // Give the body a definite width so it reflows to the pane rather
            // than laying out over-wide (responsive emails fill the pane).
            r.root->size.width = CSSLayout::Dimension::Pct(100.0f);
            bodyHost_->AddChild(scroll);
            scroll->AddChild(r.root);
            scroll->ScrollToVertical(0);
            return;
        }
        // Fall through to a text area if the build produced nothing.
    }

    // The text area is sized by the host's flex column, so it follows the pane.
    auto text = std::make_shared<UltraCanvasTextArea>("prevBodyText", 0, 0, 0, 0);
    text->SetReadOnly(true);
    text->SetEditingMode(TextAreaEditingMode::PlainText);
    text->SetWordWrap(true);
    Theme::StyleTextArea(text, /*bordered=*/false);
    text->SetText(isHtml ? HtmlToText(body) : body);
    bodyHost_->AddChild(text);
    text->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
}

void MessagePreview::Clear() {
    hasMessage_ = false;
    current_ = SourceMessage{};
    // Empty state: a quiet hint, no header chrome.
    if (subject_) {
        subject_->SetText("Select a message to read it here");
        subject_->SetFontSize(Theme::kSizeHeading);
        subject_->SetFontWeight(FontWeight::Normal);
        subject_->SetTextColor(Theme::kTextMuted);
    }
    if (header_) header_->SetVisible(false);
    if (rule_)   rule_->SetVisible(false);
    if (from_)    from_->SetText("");
    if (to_)      to_->SetText("");
    if (date_)    date_->SetText("");
    if (avatarHost_) avatarHost_->ClearChildren();
    if (bodyHost_) bodyHost_->ClearChildren();
    attachmentStrip_.SetAttachments({});
}

void MessagePreview::Show(const MessageEnvelope& env) {
    hasMessage_ = true;
    curAccount_ = env.accountId;

    // Decode RFC 2047 encoded-words for display (idempotent: messages synced
    // before header decoding are still stored raw).
    const std::string subject  = UltraNet_MimeDecodeHeader(env.subject);
    const std::string fromName = UltraNet_MimeDecodeHeader(env.fromName);
    std::vector<std::string> toList = env.to;
    for (auto& addr : toList) addr = UltraNet_MimeDecodeHeader(addr);

    // Name on the first line; address and recipients on the second, with the
    // full sender in the tooltip.
    const std::string sender = fromName.empty()
        ? env.fromAddr : (fromName + " <" + env.fromAddr + ">");
    std::string meta = fromName.empty() ? "" : env.fromAddr;
    if (!toList.empty()) meta += (meta.empty() ? "to " : "  ·  to ") + JoinAddresses(toList);
    if (subject_) {
        subject_->SetText(subject.empty() ? "(no subject)" : subject);
        subject_->SetFontSize(kSubjectFont);
        subject_->SetFontWeight(FontWeight::Bold);
        subject_->SetTextColor(Theme::kTextPrimary);
        subject_->SetTooltip(subject);
    }
    if (header_) header_->SetVisible(true);
    if (rule_)   rule_->SetVisible(true);
    if (from_) {
        from_->SetText(fromName.empty() ? env.fromAddr : fromName);
        from_->SetTooltip(sender);
    }
    if (to_) {
        to_->SetText(meta);
        to_->SetTooltip(meta);
    }
    if (date_)    date_->SetText(FormatShortDate(env.date));
    if (avatarHost_) {
        avatarHost_->ClearChildren();
        avatarHost_->AddChild(Theme::MakeAvatar("prevAvatar",
                                                SenderInitial(fromName, env.fromAddr),
                                                kAvatarSide));
    }

    // Load the cached body (.eml) and decode it.
    fs::path path = fs::path(mailDir_) / env.accountId / SanitizeFolder(env.folder)
                  / (std::to_string(env.uid) + ".eml");
    // Read through the framework's file loader: a cached body is never
    // compressed, but unlike a bare ifstream it reports why a read failed, so an
    // unreadable file says so instead of looking as if it were never downloaded.
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        RenderBody("(message body not downloaded yet)", false);
        attachmentStrip_.SetAttachments({});
        current_.body.clear();
        current_.attachments.clear();
    } else if (auto loaded = UltraCanvas::UltraCanvasFileLoader::LoadFile(path.string());
               !loaded.success) {
        RenderBody("(this message's body could not be read)\n\n"
                   + (loaded.error.empty() ? path.string() : loaded.error), false);
        attachmentStrip_.SetAttachments({});
        current_.body.clear();
        current_.attachments.clear();
    } else {
        const std::string raw(loaded.bytes.begin(), loaded.bytes.end());
        ParsedMessage pm = MimeCodec::Parse(raw);
        RenderBody(pm.body, pm.bodyIsHtml);
        attachmentStrip_.SetAttachments(pm.attachments);
        // Reply quoting works from text; reduce HTML to text for the captured copy.
        current_.body = pm.bodyIsHtml ? HtmlToText(pm.body) : pm.body;
        current_.attachments = pm.attachments;
    }

    // Capture the selection for a possible Reply (decoded, so the quoted reply
    // header and Re: subject read correctly).
    current_.messageId = env.messageId;
    current_.fromName  = fromName;
    current_.fromAddr  = env.fromAddr;
    current_.to        = toList;
    current_.subject   = subject;
    current_.date      = FormatShortDate(env.date);
}

} // namespace UltraMail
