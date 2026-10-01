// Apps/UltraMail/ui/UltraMailComposeWindow.cpp
// Version: 0.7.0 - one view per compose window: what answers after the window
//                  closed holds the view weakly
// Version: 0.6.0 - the full formatting toolbar (UltraMailFormatBar) and the
//                  Plain text | Formatted switch
// Version: 0.5.0 - a formatted draft is edited in a rich text editor with a B /
//                  I / U / list row
// Version: 0.4.0 - flex layout that follows the window: label · input rows,
//                  a body that takes the remaining height, an attachment row
//                  shown only while there are attachments, and a bottom
//                  toolbar (Send primary, Attach…, Cancel on the right).
// Last Modified: 2026-09-30
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailComposeWindow.h"
#include "UltraCanvasPathUtf8.h"   // PathFromUtf8 / PathToUtf8

#include "UltraCanvasButton.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasModalDialog.h"

#include "UltraCloudPickerDialog.h"
#include "UltraMailRichComposer.h"
#include "UltraMailSignature.h"   // PlainBodyToRichDocument
#include "UltraMailTheme.h"

#include <sstream>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace UltraMail {

namespace {

std::string Join(const std::vector<std::string>& v) {
    std::string out;
    for (std::size_t i = 0; i < v.size(); ++i) { if (i) out += ", "; out += v[i]; }
    return out;
}

// A small media-type guess for attachments by extension; the MIME builder
// falls back to application/octet-stream for anything else.
std::string GuessMediaType(const std::string& filename) {
    std::string ext;
    if (auto dot = filename.find_last_of('.'); dot != std::string::npos)
        ext = UltraCanvas::ToLowerCase(filename.substr(dot + 1));
    static const std::map<std::string, std::string> kTypes = {
        {"pdf", "application/pdf"},   {"png", "image/png"},   {"jpg", "image/jpeg"},
        {"jpeg", "image/jpeg"},       {"gif", "image/gif"},   {"webp", "image/webp"},
        {"svg", "image/svg+xml"},     {"txt", "text/plain"},  {"md", "text/markdown"},
        {"csv", "text/csv"},          {"html", "text/html"},  {"json", "application/json"},
        {"zip", "application/zip"},   {"mp3", "audio/mpeg"},  {"mp4", "video/mp4"},
        {"docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
        {"xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
        {"pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation"},
    };
    auto it = kTypes.find(ext);
    return it == kTypes.end() ? "application/octet-stream" : it->second;
}

std::vector<std::string> Split(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) {
        std::string t = UltraCanvas::Trim(item);
        if (!t.empty()) out.push_back(t);
    }
    return out;
}

constexpr float kLabelWidth      = 56.0f;
constexpr float kAttachRowHeight = 44.0f;   // chip height + strip padding

} // namespace

std::shared_ptr<UltraCanvasContainer> ComposeView::Build() {
    root_ = CreateContainer("composeView", 0, 0, 0, 0);
    root_->SetPadding(Theme::kPagePadding);
    root_->layout.SetFlexColumn()
                 .SetFlexGap(Theme::kInnerGap)
                 .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    // Header fields: a quiet label beside a full-width input.
    auto addField = [this](const std::string& id, const std::string& caption,
                           const std::shared_ptr<UltraCanvasTextInput>& input) {
        auto row = CreateContainer(id + "Row", 0, 0, 0, Theme::kControlHeight);
        row->layout.SetFlexRow()
                   .SetFlexGap(Theme::kInnerGap)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        auto label = Theme::MakeLine(id + "Lbl", caption, Theme::kControlHeight,
                                     Theme::kSizeBody, Theme::kTextSecondary);
        label->SetElementSize(Size2Df(kLabelWidth, Theme::kControlHeight));
        row->AddChild(label);
        Theme::StyleInput(input);
        row->AddChild(input);
        input->layoutItem.SetFlexGrow(1);
        root_->AddChild(row);
        row->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    };

    to_ = CreateTextInput("cTo", 0, 0, 0, Theme::kControlHeight);
    to_->SetText(Join(draft_.to));
    to_->SetPlaceholder("recipient@example.com, …");
    addField("cTo", "To", to_);

    cc_ = CreateTextInput("cCc", 0, 0, 0, Theme::kControlHeight);
    cc_->SetText(Join(draft_.cc));
    cc_->SetPlaceholder("Optional");
    addField("cCc", "Cc", cc_);

    subject_ = CreateTextInput("cSubj", 0, 0, 0, Theme::kControlHeight);
    subject_->SetText(draft_.subject);
    subject_->SetPlaceholder("Subject");
    addField("cSubj", "Subject", subject_);

    root_->AddChild(Theme::MakeDivider("cRule"));

    // The formatting toolbar, with Plain text | Formatted at its right end.
    // The tools act on the rich editor, and only while the body is formatted.
    FormatBar::Options barOptions;
    barOptions.idPrefix = "c";
    barOptions.dialogParent = parent_;
    // Asked on every click, and again when a Link… or Picture… dialog
    // answers - by then the window may be closed and this view gone.
    barOptions.editor = [weak = weak_from_this()]() -> UltraCanvasRichTextEdit* {
        auto self = weak.lock();
        return self && self->formatted_ ? self->rich_.get() : nullptr;
    };
    formatBar_ = FormatBar::Build(barOptions);
    mode_ = CreateSegmentedControl("cMode", 0, 0, 160, Theme::kControlHeight);
    Theme::StyleSegmented(mode_);
    mode_->AddSegment("Plain text");
    mode_->AddSegment("Formatted");
    mode_->SetTooltip("Plain text, or formatted text (sent as HTML with a plain-text version)");
    mode_->onSegmentSelected = [this](int segment) {
        if (!switchingMode_) SetFormatted(segment == 1);
    };
    formatBar_.paragraphRow->AddChild(mode_);
    root_->AddChild(formatBar_.root);
    formatBar_.root->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    // The body takes whatever height the fields and the toolbars leave. Both
    // editors exist; the mode decides which one shows and holds the message.
    body_ = std::make_shared<UltraCanvasTextArea>("cBody", 0, 0, 0, 0);
    body_->SetEditingMode(TextAreaEditingMode::PlainText);
    body_->SetWordWrap(true);
    Theme::StyleTextArea(body_, /*bordered=*/false);
    body_->SetText(draft_.body);
    root_->AddChild(body_);
    body_->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    rich_ = CreateRichTextEdit("cRich", 0, 0, 0, 0);
    RichTextEditStyle richStyle = rich_->GetStyle();
    richStyle.baseFont.fontSize = Theme::kSizeBody + 1.0f;   // as the plain body
    richStyle.drawBorder = false;
    rich_->SetStyle(richStyle);
    rich_->SetDocument(draft_.richBody ? draft_.richBody : PlainBodyToRichDocument(""));
    rich_->GetEditor().SetCaret(RichDocPosition(0, 0));  // the empty line above the quote
    root_->AddChild(rich_);
    rich_->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    formatted_ = draft_.richBody != nullptr;
    ShowMode();

    // Attachment chips between the body and the toolbar; the row is shown
    // only while there is something to show (forwards carry the original's
    // attachments).
    attachWrap_ = CreateContainer("cAttachWrap", 0, 0, 0, kAttachRowHeight);
    ContainerStyle stripStyle;
    stripStyle.autoShowScrollbars = false;
    attachWrap_->SetContainerStyle(stripStyle);
    attachWrap_->AddChild(attachments_.Build());
    root_->AddChild(attachWrap_);
    attachWrap_->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    RefreshAttachments();

    // Bottom toolbar: Send first, the attach actions beside it, Cancel apart
    // on the right so it cannot be hit by accident.
    auto toolbar = CreateContainer("cToolbar", 0, 0, 0, Theme::kToolbarHeight);
    toolbar->layout.SetFlexRow()
                   .SetFlexGap(Theme::kInnerGap)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Center);

    auto sendBtn = CreateButton("cSend", 0, 0, 100, Theme::kControlHeight, "Send");
    Theme::FitToLabel(sendBtn, 100);
    Theme::StylePrimary(sendBtn);
    sendBtn->onClick = [this]() { if (onSend) onSend(CollectDraft()); };
    toolbar->AddChild(sendBtn);

    auto attachBtn = CreateButton("cAttach", 0, 0, 120, Theme::kControlHeight, "Attach file…");
    Theme::FitToLabel(attachBtn, 120);
    Theme::StyleSecondary(attachBtn);
    attachBtn->onClick = [this]() { ChooseFileToAttach(); };
    toolbar->AddChild(attachBtn);

    auto cloudBtn = CreateButton("cCloud", 0, 0, 192, Theme::kControlHeight,
                                 "Attach cloud link…");
    Theme::FitToLabel(cloudBtn, 192);
    Theme::StyleSecondary(cloudBtn);
    cloudBtn->SetIcon(NormalizePath(GetResourcesDir() + "media/icons/cloud.svg"));
    cloudBtn->SetIconPosition(ButtonIconPosition::Left);
    cloudBtn->SetIconSize(16, 16);
    cloudBtn->SetIconSpacing(6);
    cloudBtn->SetUseIconAsMask(true);
    cloudBtn->SetTooltip(cloud_ ? "Upload a file to cloud storage, or pick one, and put its share link into the message"
                                : "Cloud storage is not set up in this application");
    cloudBtn->onClick = [this]() { ChooseCloudLink(); };
    toolbar->AddChild(cloudBtn);

    toolbar->AddStretchSpacer(1);

    auto cancelBtn = CreateButton("cCancel", 0, 0, 90, Theme::kControlHeight, "Cancel");
    Theme::FitToLabel(cancelBtn, 90);
    Theme::StyleSecondary(cancelBtn);
    cancelBtn->onClick = [this]() { if (onCancel) onCancel(); };
    toolbar->AddChild(cancelBtn);

    root_->AddChild(toolbar);
    toolbar->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    return root_;
}

void ComposeView::Resize(float width, float height) {
    if (root_) root_->SetElementSize(Size2Df(width, height));
}

void ComposeView::RefreshAttachments() {
    attachments_.SetAttachments(draft_.attachments);
    if (attachWrap_) attachWrap_->SetVisible(!draft_.attachments.empty());
}

bool ComposeView::AttachFile(const std::string& path) {
    std::ifstream is(UltraCanvas::PathFromUtf8(path), std::ios::binary);
    if (!is) return false;
    Attachment a;
    a.filename  = PathToUtf8(PathFromUtf8(path).filename());
    a.mediaType = GuessMediaType(a.filename);
    a.data.assign(std::istreambuf_iterator<char>(is), std::istreambuf_iterator<char>());
    draft_.attachments.push_back(std::move(a));
    RefreshAttachments();
    return true;
}

void ComposeView::ShowMode() {
    if (body_) body_->SetVisible(!formatted_);
    if (rich_) rich_->SetVisible(formatted_);
    formatBar_.SetToolsVisible(formatted_);
    if (mode_) {
        switchingMode_ = true;
        mode_->SetSelectedIndex(formatted_ ? 1 : 0);
        switchingMode_ = false;
    }
}

void ComposeView::SwitchToPlain() {
    if (!body_ || !rich_) return;
    body_->SetText(rich_->GetPlainText());
    formatted_ = false;
    ShowMode();
    body_->SetFocus(true);
}

void ComposeView::SetFormatted(bool formatted, bool ask) {
    if (!body_ || !rich_) return;
    if (formatted == formatted_) { ShowMode(); return; }

    if (formatted) {
        // The text as it is, "> " quotes as quote bars; written on at the top.
        rich_->SetDocument(PlainBodyToRichDocument(body_->GetText()));
        rich_->GetEditor().SetCaret(RichDocPosition(0, 0));
        formatted_ = true;
        ShowMode();
        rich_->SetFocus(true);
        return;
    }

    const auto& doc = rich_->GetDocument();
    const std::string text = rich_->GetPlainText();
    const bool something = (doc && !doc->media.empty())
                        || text.find_first_not_of(" \t\r\n") != std::string::npos;
    if (!ask || !something) { SwitchToPlain(); return; }
    UltraCanvasDialogManager::ShowConfirmation(
        "Send this message as plain text? Its formatting, links and pictures are removed.",
        "Plain text",
        [weak = weak_from_this()](bool confirmed) {
            auto self = weak.lock();
            if (!self) return;   // the window closed while the question was open
            if (confirmed) self->SwitchToPlain();
            else self->ShowMode();   // stays formatted: the switch goes back
        },
        parent_);
}

void ComposeView::InsertLink(const std::string& name, const std::string& url) {
    if (formatted_ && rich_) {
        // At the caret, the address as a link.
        rich_->InsertText(name + ": ");
        UCRichDocumentEditor& editor = rich_->GetEditor();
        const RichDocPosition start = editor.GetCaret();
        rich_->InsertText(url);
        editor.SetSelection(start, editor.GetCaret());
        rich_->SetLink(url);
        editor.SetCaret(editor.GetSelectionRange().end);
        rich_->InvalidateDocument();
        return;
    }
    if (!body_) return;
    std::string text = body_->GetText();
    if (!text.empty() && text.back() != '\n') text += "\n";
    text += "\n" + name + ": " + url + "\n";
    body_->SetText(text);
}

void ComposeView::ChooseFileToAttach() {
    FileDialogOptions options;
    options.title = "Attach file";
    options.parentWindow = parent_;
    UltraCanvasFileLoader::OpenFileDialog(
        options, [weak = weak_from_this()](DialogResult result, const std::string& path) {
            auto self = weak.lock();
            if (!self || result != DialogResult::OK || path.empty()) return;
            if (!self->AttachFile(path))
                UltraCanvasDialogManager::ShowError("Could not read " + path, "Attach file",
                                                    nullptr, self->parent_);
        });
}

void ComposeView::ChooseCloudLink() {
    if (!cloud_) {
        UltraCanvasDialogManager::ShowInformation(
            "Cloud storage is not set up in this application.", "Attach cloud link",
            nullptr, parent_);
        return;
    }
    UltraCloud::ShowCloudLinkPicker(parent_, *cloud_,
        [weak = weak_from_this()](const UltraCloud::CloudLinkPick& pick) {
            if (auto self = weak.lock()) self->InsertLink(pick.entry.name, pick.link.url);
        });
}

Draft ComposeView::CollectDraft() const {
    Draft d = draft_;   // keep from/identity, in-reply-to, references, attachments
    if (to_)      d.to = Split(to_->GetText());
    if (cc_)      d.cc = Split(cc_->GetText());
    if (subject_) d.subject = subject_->GetText();
    if (formatted_ && rich_) {
        // The edited document goes out as HTML with a plain-text version.
        d.richBody = rich_->GetDocument();
        RenderRichBody(d);
    } else {
        // Plain text - also a formatted draft switched to plain text.
        d.body = body_ ? body_->GetText() : std::string();
        d.richBody.reset();
        d.bodyIsHtml = false;
        d.textBody.clear();
        d.inlineParts.clear();
    }
    return d;
}

} // namespace UltraMail
