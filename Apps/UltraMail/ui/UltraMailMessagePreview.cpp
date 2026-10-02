// Apps/UltraMail/ui/UltraMailMessagePreview.cpp
// Version: 0.7.0 - Settings: plain-text view, text size, trusted-website pictures
// Version: 0.6.1 - the HTML body is built for the pane width (@media queries)
// Version: 0.6.0 - Reply / Forward hand over the HTML body and its pictures
// Version: 0.5.0 - sender badge instead of the initial avatar; the cached body
//                  is scanned on first read and the verdict stored, with a
//                  warning strip above suspicious and scam messages.
// Version: 0.4.3 - From/To are auto-height labels (never cropped); the HTML body
//                  fills the pane width (reflows) and gets a horizontal scrollbar
//                  when content cannot reflow, instead of being clipped.
// Last Modified: 2026-09-29
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailMessagePreview.h"
#include "UltraCanvasPathUtf8.h"   // PathFromUtf8 / PathToUtf8

#include "UltraCanvasConfig.h"
#include "UltraCanvasFileLoader.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasTextArea.h"
#include "HTMLReader/HTMLElementBuilder.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasUtils.h"      // OpenURL

#include "UltraMailMimeCodec.h"
#include "UltraMailTheme.h"

#include <UltraNet/UltraNetHttp.h>
#include <UltraNet/UltraNetMime.h>

#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

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

    // Actions row above the subject: Reply · Forward · Junk · Delete · More.
    actions_ = CreateContainer("prevActions", 0, 0, 0, 0);
    actions_->layout.SetFlexRow()
                    .SetFlexGap(Theme::kInnerGap)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    if (auto s = actions_->GetContainerStyle(); true) {
        s.autoShowScrollbars = false;   // chrome row: never fabricate a scrollbar
        actions_->SetContainerStyle(s);
    }
    root_->AddChild(actions_);
    actions_->layoutItem.SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    // One styled action button (icon optional, tinted with the text colour).
    auto makeActionButton = [&](const std::string& id, const std::string& text,
                                const std::string& icon) {
        auto b = CreateButton(id, 0, 0, 84, 30, text);
        Theme::FitToLabel(b, 84);
        Theme::StyleSecondary(b);
        if (!icon.empty()) {
            b->SetIcon(NormalizePath(GetResourcesDir() + "media/icons/" + icon));
            b->SetIconPosition(ButtonIconPosition::Left);
            b->SetIconSize(14, 14);
            b->SetIconSpacing(6);
            b->SetUseIconAsMask(true);
        }
        actions_->AddChild(b);
        b->layoutItem.SetFlexShrink(0);
        return b;
    };

    // Reply / Forward resolve the account's own identity for the quoted header.
    auto selfIdentity = [this](std::string& selfName, std::string& selfAddr) {
        for (const auto& a : accounts_)
            if (a.accountId == curAccount_) { selfName = a.displayName; selfAddr = a.email; }
    };

    // The message as a reply or forward sees it, with its pictures: the
    // message's own, and remote ones only when already loaded - composing
    // never fetches anything. Used while the reply is built, which happens
    // before the handler returns.
    auto source = [this]() {
        SourceMessage src = current_;
        src.image = [this](const std::string& url) -> std::vector<uint8_t> {
            switch (ClassifyImageSource(url, inlineImages_)) {
                case ImageSource::Embedded:
                    return ResolveEmbeddedImage(url, inlineImages_);
                case ImageSource::Remote:
                    if (auto it = remoteCache_.find(url); it != remoteCache_.end()) return it->second;
                    return {};
                case ImageSource::Other:
                    break;
            }
            return {};
        };
        return src;
    };

    auto replyBtn = makeActionButton("prevReply", "Reply", "undo.svg");
    replyBtn->onClick = [this, selfIdentity, source]() {
        if (!onReply || !hasMessage_) return;
        std::string selfName, selfAddr; selfIdentity(selfName, selfAddr);
        onReply(source(), selfName, selfAddr);
    };

    auto forwardBtn = makeActionButton("prevForward", "Forward", "redo.svg");
    forwardBtn->onClick = [this, selfIdentity, source]() {
        if (!onForward || !hasMessage_) return;
        std::string selfName, selfAddr; selfIdentity(selfName, selfAddr);
        onForward(source(), selfName, selfAddr);
    };

    junkBtn_ = makeActionButton("prevJunk", "Junk", "circle-stop.svg");
    junkBtn_->onClick = [this]() { if (onJunk && hasMessage_) onJunk(curEnv_); };

    auto deleteBtn = makeActionButton("prevDelete", "Delete", "delete.svg");
    deleteBtn->onClick = [this]() { if (onDelete && hasMessage_) onDelete(curEnv_); };

    // "More" opens a popup menu with the less-frequent actions.
    auto moreBtn = makeActionButton("prevMore", "More", "");
    moreMenu_ = std::make_shared<UltraCanvasMenu>("prevMoreMenu", 0, 0, 200, 0);
    moreMenu_->SetMenuType(MenuType::PopupMenu);
    moreMenu_->AddItem(MenuItemData::Action("Mark as Unread", [this]() {
        if (onMarkUnread && hasMessage_) onMarkUnread(curEnv_);
    }));
    moreMenu_->AddItem(MenuItemData::Action("View source", [this]() {
        if (onViewSource && hasMessage_) onViewSource(current_.subject, curRaw_);
    }));
    UltraCanvasButton* moreRaw = moreBtn.get();
    moreBtn->onClick = [this, moreRaw]() {
        auto* win = moreRaw->GetWindow();
        if (!win || !moreMenu_) return;
        // Anchor the menu under the button (window coordinates).
        Rect2Df b = moreRaw->GetBoundsInWindow();
        win->AddChild(moreMenu_);
        moreMenu_->OpenMenu(Point2Di(static_cast<int>(b.x),
                                     static_cast<int>(b.y + b.height)),
                            *win, PopupElementSettings());
    };

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

    root_->AddChild(header);
    header->layoutItem.SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    rule_ = Theme::MakeDivider("prevRule");
    root_->AddChild(rule_);

    // The warning strip: hidden for ordinary mail, shown above the body when
    // the content scan found something worth stopping the reader for. It says
    // what and why — never "blocked", because the message is still readable.
    warning_ = CreateContainer("prevWarning", 0, 0, 0, 0);
    warning_->layout.SetFlexColumn()
                    .SetFlexGap(2);
    warning_->SetPadding(8.0f, 10.0f);
    warningTitle_ = Theme::MakeText("prevWarningTitle", "", Theme::kSizeBody,
                                    Theme::kTrustScam, FontWeight::Bold);
    warningTitle_->SetWrap(TextWrap::WrapWord);
    warningText_ = Theme::MakeText("prevWarningText", "", Theme::kSizeSecondary,
                                   Theme::kTextPrimary);
    warningText_->SetWrap(TextWrap::WrapWord);
    warning_->AddChild(warningTitle_);
    warning_->AddChild(warningText_);
    warningTitle_->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    warningText_->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    root_->AddChild(warning_);
    warning_->layoutItem.SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    warning_->SetVisible(false);

    // The remote-images bar: hidden unless the HTML body references images on
    // the web that have not been loaded (see RenderBody / UpdateRemoteBar).
    // Text above, buttons below: the pane can be narrow and the sender's
    // address long, and a row would push the buttons out of sight.
    remoteBar_ = CreateContainer("prevRemoteBar", 0, 0, 0, 0);
    remoteBar_->layout.SetFlexColumn()
                      .SetFlexGap(6)
                      .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    remoteBar_->SetPadding(6.0f, 10.0f);
    remoteBar_->SetBackgroundColor(Theme::kSidebar);
    remoteBar_->SetBorders(1.0f, Theme::kCardBorder, Theme::kControlRadius);
    remoteText_ = Theme::MakeText("prevRemoteText", "", Theme::kSizeSecondary,
                                  Theme::kTextSecondary);
    remoteText_->SetWrap(TextWrap::WrapWord);
    remoteBar_->AddChild(remoteText_);
    remoteText_->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    auto remoteButtons = CreateContainer("prevRemoteButtons", 0, 0, 0, Theme::kControlHeight);
    remoteButtons->layout.SetFlexRow()
                         .SetFlexGap(Theme::kInnerGap)
                         .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    remoteBar_->AddChild(remoteButtons);
    remoteShow_ = CreateButton("prevRemoteShow", 0, 0, 100, Theme::kControlHeight, "Show images");
    Theme::FitToLabel(remoteShow_, 100);
    Theme::StyleSecondary(remoteShow_);
    remoteShow_->onClick = [this]() { FetchRemoteImages(); };
    remoteButtons->AddChild(remoteShow_);
    remoteAlways_ = CreateButton("prevRemoteAlways", 0, 0, 120, Theme::kControlHeight,
                                 "Always from this sender");
    Theme::FitToLabel(remoteAlways_, 120);
    Theme::StyleSecondary(remoteAlways_);
    remoteAlways_->onClick = [this]() {
        if (onAlwaysAllowRemoteImages && !curEnv_.fromAddr.empty())
            onAlwaysAllowRemoteImages(curEnv_.fromAddr);
        FetchRemoteImages();
    };
    remoteButtons->AddChild(remoteAlways_);
    root_->AddChild(remoteBar_);
    remoteBar_->layoutItem.SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    remoteBar_->SetVisible(false);

    // Body host: takes the remaining height; RenderBody() fills it with either
    // a read-only text area (plain text) or the HTMLReader-built element tree.
    bodyHost_ = CreateContainer("prevBodyHost", 0, 0, 0, 0);
    bodyHost_->layout.SetFlexColumn()
                     .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    // The body host IS the scroll view for a tall message, so it opts into
    // scrolling (containers do not scroll unless asked): the vertical bar is
    // auto, but never a horizontal one — HTML reflows to the width, and when
    // the vertical bar appears it must not fabricate horizontal overflow.
    if (auto s = bodyHost_->GetContainerStyle(); true) {
        s.autoShowScrollbars = true;
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

void MessagePreview::ReRender() {
    if (!bodyHost_ || !hasMessage_) return;
    blockedRemote_.clear();
    RenderBody(lastBody_, lastIsHtml_);
    FetchTrustedHostImages();
    UpdateRemoteBar();
}

void MessagePreview::RenderBody(const std::string& body, bool isHtml) {
    if (!bodyHost_) return;
    if (&body != &lastBody_) lastBody_ = body;
    lastIsHtml_ = isHtml;
    bodyHost_->ClearChildren();

    // Settings > Reading > "as plain text": no layout and nothing fetched.
    if (isHtml && showHtml) {
        // Full render through the HTMLReader element builder: the CSSLayout
        // engine measures and lays out a native UltraCanvas tree (containers +
        // Pango-markup labels + images).
        HTML::BuildOptions opts;
        opts.style.baseFontSizePx = bodyFontSizePx;   // 12px ≈ the 9pt UI font
        opts.enableImages = true;
        // @media queries (a newsletter's side-by-side columns from 480px up)
        // are answered for the pane the message is shown in.
        if (bodyHost_->GetWidth() > 0.f) opts.viewportWidth = bodyHost_->GetWidth();
        // Embedded images from the message; remote ones only once loaded.
        opts.resourceLoader = [this](const std::string& src) { return LoadBodyImage(src); };
        // Links open in the browser (web and mail addresses only - never a
        // file: or javascript: target a message could carry).
        opts.onLinkActivated = [](const std::string& href) {
            std::string lower = href;
            for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (lower.rfind("http://", 0) == 0 || lower.rfind("https://", 0) == 0 ||
                lower.rfind("mailto:", 0) == 0)
                UltraCanvas::OpenURL(href);
        };
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
            // A deliberate scroll view, so it opts in (containers do not
            // scroll unless asked): the vertical bar for a tall message, and a
            // horizontal one too, so content that genuinely cannot reflow
            // (fixed-width tables, large images) can be scrolled to instead of
            // being clipped.
            {
                ContainerStyle scrollStyle = scroll->GetContainerStyle();
                scrollStyle.autoShowScrollbars = true;
                scroll->SetContainerStyle(scrollStyle);
            }
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

std::vector<uint8_t> MessagePreview::LoadBodyImage(const std::string& src) {
    switch (ClassifyImageSource(src, inlineImages_)) {
        case ImageSource::Embedded:
            return ResolveEmbeddedImage(src, inlineImages_);
        case ImageSource::Remote:
            if (auto it = remoteCache_.find(src); it != remoteCache_.end()) return it->second;
            blockedRemote_.insert(src);
            return {};
        case ImageSource::Other:
            break;
    }
    return {};
}

void MessagePreview::UpdateRemoteBar() {
    if (!remoteBar_) return;
    const bool show = hasMessage_ && !blockedRemote_.empty() && !remoteAllowed_;
    remoteBar_->SetVisible(show || fetchingRemote_);
    if (!show && !fetchingRemote_) return;
    if (fetchingRemote_) {
        remoteText_->SetText("Loading images\xE2\x80\xA6");
        remoteShow_->SetVisible(false);
        remoteAlways_->SetVisible(false);
        return;
    }
    const std::size_t n = blockedRemote_.size();
    std::string text = std::to_string(n) + (n == 1 ? " image is" : " images are") +
                       " on the web and not loaded, so the sender cannot see that you "
                       "opened this message.";
    if (remoteDangerous_) text += " This message looks suspicious.";
    remoteText_->SetText(text);
    remoteShow_->SetVisible(true);
    // Never offer to trust a sender whose message looks like spam or a scam.
    remoteAlways_->SetVisible(!remoteDangerous_ && !curEnv_.fromAddr.empty());
    if (!remoteDangerous_ && !curEnv_.fromAddr.empty())
        remoteAlways_->SetText("Always from " + curEnv_.fromAddr);
}

void MessagePreview::FetchTrustedHostImages() {
    if (remoteAllowed_ || remoteDangerous_ || !remoteImageHostTrusted ||
        blockedRemote_.empty())
        return;
    std::vector<std::string> urls;
    for (const auto& u : blockedRemote_)
        if (remoteImageHostTrusted(u)) urls.push_back(u);
    if (!urls.empty()) FetchSomeRemoteImages(urls);
}

void MessagePreview::FetchSomeRemoteImages(const std::vector<std::string>& urls) {
    if (fetchingRemote_ || urls.empty()) return;
    fetchingRemote_ = true;
    UpdateRemoteBar();
    const uint64_t token = showToken_;
    std::thread([this, urls, token]() {
        auto results = std::make_shared<std::map<std::string, std::vector<uint8_t>>>();
        for (const auto& src : urls) {
            const std::string url = src.rfind("//", 0) == 0 ? "https:" + src : src;
            UltraNetHttpOptions options;
            options.timeoutMs = 15000;
            options.connectTimeoutMs = 8000;
            options.maxReceiveSize = 5 * 1024 * 1024;
            UltraNetResponse response;
            if (UltraNet_HttpGet(url, response, options) && response.statusCode >= 200 &&
                response.statusCode < 300 && !response.body.empty())
                (*results)[src] = std::move(response.body);
            else
                (*results)[src] = {};
        }
        auto* app = UltraCanvas::UltraCanvasApplicationBase::GetCurrent();
        if (!app) return;
        app->PostToUIThread([this, results, token]() {
            fetchingRemote_ = false;
            if (remoteCache_.size() + results->size() > 400) remoteCache_.clear();
            for (auto& [src, bytes] : *results) remoteCache_[src] = std::move(bytes);
            if (token != showToken_) return;
            // The others stay blocked: the new render lists them again.
            blockedRemote_.clear();
            RenderBody(curHtml_, true);
            UpdateRemoteBar();
        });
    }).detach();
}

void MessagePreview::FetchRemoteImages() {
    if (fetchingRemote_ || blockedRemote_.empty()) return;
    // At most this many per message: a newsletter rarely has more, and a
    // message with hundreds is not one to fetch blindly.
    constexpr std::size_t kMaxImages = 60;
    std::vector<std::string> urls;
    for (const auto& u : blockedRemote_) {
        if (urls.size() >= kMaxImages) break;
        urls.push_back(u);
    }
    fetchingRemote_ = true;
    UpdateRemoteBar();
    const uint64_t token = showToken_;
    std::thread([this, urls, token]() {
        auto results = std::make_shared<std::map<std::string, std::vector<uint8_t>>>();
        for (const auto& src : urls) {
            const std::string url = src.rfind("//", 0) == 0 ? "https:" + src : src;
            UltraNetHttpOptions options;
            options.timeoutMs = 15000;
            options.connectTimeoutMs = 8000;
            options.maxReceiveSize = 5 * 1024 * 1024;   // an image, not a download
            UltraNetResponse response;
            if (UltraNet_HttpGet(url, response, options) && response.statusCode >= 200 &&
                response.statusCode < 300 && !response.body.empty())
                (*results)[src] = std::move(response.body);
            else
                (*results)[src] = {};   // tried: do not ask again for this message
        }
        auto* app = UltraCanvas::UltraCanvasApplicationBase::GetCurrent();
        if (!app) return;
        app->PostToUIThread([this, results, token]() {
            fetchingRemote_ = false;
            if (remoteCache_.size() + results->size() > 400) remoteCache_.clear();
            for (auto& [src, bytes] : *results) remoteCache_[src] = std::move(bytes);
            if (token != showToken_) return;          // another message is on screen
            blockedRemote_.clear();
            remoteAllowed_ = true;                   // for this message, from now on
            RenderBody(curHtml_, true);
            UpdateRemoteBar();
        });
    }).detach();
}

MessageSecurity MessagePreview::SecurityFor(const MessageEnvelope& env,
                                            const std::string& raw) {
    MessageSecurity sec;
    if (store_) store_->GetSecurity(env.accountId, env.folder, env.uid, sec);
    if (raw.empty()) return sec;
    bool changed = false;

    // First read of this message: scan the cached body once and keep the
    // verdict, so the list can colour the row without parsing every .eml.
    if (!sec.Scanned()) {
        const ThreatReport report = ScanRawMessage(raw);
        sec.level  = report.level;
        sec.score  = report.score;
        sec.bulk   = report.bulk;
        sec.reason = report.Summary();
        changed = true;
    }
    // And its attachment count, for the list's paperclip, when the body was
    // downloaded before counts were kept.
    if (sec.attachments < 0) {
        sec.attachments = MimeCodec::CountAttachments(raw);
        changed = true;
    }
    if (!changed) return sec;
    if (store_) store_->SetSecurity(env.accountId, env.folder, env.uid, sec);
    if (onSecurityScanned) onSecurityScanned(env, sec);
    return sec;
}

void MessagePreview::ShowSecurityWarning(const SenderStatus& status,
                                         const MessageSecurity& security,
                                         const std::string& raw) {
    if (!warning_ || !warningTitle_ || !warningText_) return;

    // Only the two verdicts worth interrupting a reader for. Advertisements and
    // unknown senders are the badge's business, not a banner's.
    if (!status.Dangerous()) {
        warning_->SetVisible(false);
        return;
    }

    // A dangerous message whose button leads off the sender's own domain is
    // what phishing looks like: say so plainly, and show both domains so the
    // reader can see the mismatch for themselves rather than take our word.
    // Only on the scan's own verdict: a newsletter that is merely sitting in
    // Junk links to its tracking domain too, and is not phishing for that.
    DomainMismatch mismatch;
    if (security.level >= ThreatLevel::Suspicious) mismatch = FindDomainMismatchInRaw(raw);
    const bool phishing = mismatch.found;

    const bool scam = status.cls == SenderClass::Scam || phishing;
    const Color accent = scam ? Theme::kTrustScam : Theme::kTrustSpam;
    warning_->SetBackgroundColor(scam ? Theme::kTrustScamSoft : Theme::kTrustSpamSoft);
    warning_->SetBorders(1.0f, accent, Theme::kControlRadius);
    warningTitle_->SetTextColor(accent);
    warningTitle_->SetText(std::string("\xE2\x9A\xA0 ") +
        (phishing ? "Warning: This is likely a phishing\xC2\xB2 email!"
         : status.cls == SenderClass::Scam
              ? "This message looks like a scam or phishing attempt"
              : "Parts of this message do not add up"));

    std::string text;
    if (phishing) {
        text = "Mismatch of domains\n"
               "Sender domain: " + mismatch.senderDomain + "\n" +
               (mismatch.isButton ? "Button domain: " : "Link domain: ") +
               mismatch.linkDomain;
        if (!mismatch.linkText.empty())
            text += "  (\xE2\x80\x9C" + mismatch.linkText + "\xE2\x80\x9D)";
        text += "\n";
    } else {
        text = status.reason;
    }
    if (!security.reason.empty()) text += (text.empty() ? "" : "\n") + security.reason;
    text += "\nDo not sign in, pay or reply through the links in this message unless you "
            "are sure who sent it.";
    if (phishing)
        text += "\n\n\xC2\xB2 Phishing emails are emails that try to get your credentials "
                "to hack your accounts on other websites.";
    warningText_->SetText(text);
    warning_->SetVisible(true);
}

void MessagePreview::Clear() {
    hasMessage_ = false;
    ++showToken_;
    curHtml_.clear();
    inlineImages_ = InlineImages{};
    blockedRemote_.clear();
    remoteAllowed_ = false;
    fetchingRemote_ = false;
    if (remoteBar_) remoteBar_->SetVisible(false);
    current_ = SourceMessage{};
    curEnv_ = MessageEnvelope{};
    curRaw_.clear();
    // Empty state: a quiet hint, no header chrome.
    if (subject_) {
        subject_->SetText("Select a message to read it here");
        subject_->SetFontSize(Theme::kSizeHeading);
        subject_->SetFontWeight(FontWeight::Normal);
        subject_->SetTextColor(Theme::kTextMuted);
    }
    if (actions_) actions_->SetVisible(false);
    if (header_)  header_->SetVisible(false);
    if (rule_)    rule_->SetVisible(false);
    if (warning_) warning_->SetVisible(false);
    if (from_)    from_->SetText("");
    if (to_)      to_->SetText("");
    if (date_)    date_->SetText("");
    if (avatarHost_) avatarHost_->ClearChildren();
    if (bodyHost_) bodyHost_->ClearChildren();
    attachmentStrip_.SetAttachments({});
}

void MessagePreview::Show(const MessageEnvelope& env) {
    hasMessage_ = true;
    ++showToken_;
    curHtml_.clear();
    inlineImages_ = InlineImages{};
    blockedRemote_.clear();
    remoteAllowed_ = false;
    fetchingRemote_ = false;
    curAccount_ = env.accountId;
    curEnv_     = env;   // identity for Delete / Junk / Mark-Unread

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
    if (actions_) actions_->SetVisible(true);
    // "Junk" moves a message into the Junk mailbox — pointless when already there.
    if (junkBtn_) junkBtn_->SetVisible(!junkFolder_);
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

    // The badge (and the warning strip) need the scan verdict, which needs the
    // body — so both are filled in after it has been loaded, below.
    std::string raw;
    std::string pendingBody;
    bool pendingHtml = false, havePending = false;

    // Load the cached body (.eml) and decode it.
    fs::path path = PathFromUtf8(mailDir_) / env.accountId / SanitizeFolder(env.folder)
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
        curRaw_.clear();
    } else if (auto loaded = UltraCanvas::UltraCanvasFileLoader::LoadFile(PathToUtf8(path));
               !loaded.success) {
        RenderBody("(this message's body could not be read)\n\n"
                   + (loaded.error.empty() ? PathToUtf8(path) : loaded.error), false);
        attachmentStrip_.SetAttachments({});
        current_.body.clear();
        current_.attachments.clear();
        curRaw_.clear();
    } else {
        raw.assign(loaded.bytes.begin(), loaded.bytes.end());
        curRaw_ = raw;   // kept for "View source"
        ParsedMessage pm = MimeCodec::Parse(raw);
        // Rendered below, once the verdict says whether remote images may load.
        pendingBody = pm.body;
        pendingHtml = pm.bodyIsHtml;
        havePending = true;
        attachmentStrip_.SetAttachments(pm.attachments);
        // The text for a plain reply, and the HTML a formatted one is built
        // from (UltraMailRichComposer).
        current_.body = pm.bodyIsHtml ? HtmlToText(pm.body) : pm.body;
        current_.bodyHtml = pm.bodyIsHtml ? pm.body : std::string();
        current_.attachments = pm.attachments;
    }

    // Who the message is from, in the same badge the list row wears, and the
    // warning strip when the scan found something.
    const MessageSecurity security = SecurityFor(env, raw);
    const SenderStatus    status   = badges_.Classify(env, security, junkFolder_);
    if (avatarHost_) {
        avatarHost_->ClearChildren();
        avatarHost_->AddChild(MakeSenderBadgeElement(
            "prevBadge", badges_.Resolve(env, security, junkFolder_), kAvatarSide));
    }
    if (from_) {
        // The sender line carries the verdict in words, so the badge's colour
        // is never the only place it is said.
        std::string tip = sender + "\n" + DisplayName(status.cls);
        if (!status.reason.empty()) tip += " \xE2\x80\x94 " + status.reason;
        if (!security.reason.empty()) tip += "\n" + security.reason;
        from_->SetTooltip(tip);
    }
    ShowSecurityWarning(status, security, raw);

    // The body, with its images: the message's own always, remote ones when
    // the reader has allowed this sender - never for a suspicious message.
    if (havePending) {
        remoteDangerous_ = status.Dangerous() || junkFolder_;
        remoteAllowed_ = !remoteDangerous_ && remoteImagesAllowed &&
                         !env.fromAddr.empty() && remoteImagesAllowed(env.fromAddr);
        if (pendingHtml) {
            curHtml_ = pendingBody;
            inlineImages_ = CollectInlineImages(raw);
        }
        RenderBody(pendingBody, pendingHtml);
        if (remoteAllowed_ && !blockedRemote_.empty()) {
            remoteAllowed_ = false;      // FetchRemoteImages sets it once loaded
            FetchRemoteImages();
        } else {
            FetchTrustedHostImages();    // pictures on trusted websites only
        }
    }
    UpdateRemoteBar();

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
