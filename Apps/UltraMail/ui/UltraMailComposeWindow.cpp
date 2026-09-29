// Apps/UltraMail/ui/UltraMailComposeWindow.cpp
// Version: 0.5.0 - a formatted draft is edited in a rich text editor with a B /
//                  I / U / list row
// Version: 0.4.0 - flex layout that follows the window: label · input rows,
//                  a body that takes the remaining height, an attachment row
//                  shown only while there are attachments, and a bottom
//                  toolbar (Send primary, Attach…, Cancel on the right).
// Last Modified: 2026-09-29
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailComposeWindow.h"
#include "UltraCanvasPathUtf8.h"   // PathFromUtf8 / PathToUtf8

#include "UltraCanvasButton.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasModalDialog.h"

#include "UltraCloudPickerDialog.h"
#include "UltraMailRichComposer.h"
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

    // The body takes whatever height the fields and the toolbar leave: the
    // formatted document of a reply or forward, or plain text.
    body_.reset();
    rich_.reset();
    if (draft_.richBody) {
        auto formatRow = BuildFormatRow();
        root_->AddChild(formatRow);
        formatRow->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

        rich_ = CreateRichTextEdit("cRich", 0, 0, 0, 0);
        RichTextEditStyle style = rich_->GetStyle();
        style.baseFont.fontSize = Theme::kSizeBody + 1.0f;   // as the plain body
        style.drawBorder = false;
        rich_->SetStyle(style);
        rich_->SetDocument(draft_.richBody);
        rich_->GetEditor().SetCaret(RichDocPosition(0, 0));  // the empty line above the quote
        root_->AddChild(rich_);
        rich_->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    } else {
        body_ = std::make_shared<UltraCanvasTextArea>("cBody", 0, 0, 0, 0);
        body_->SetEditingMode(TextAreaEditingMode::PlainText);
        body_->SetWordWrap(true);
        Theme::StyleTextArea(body_, /*bordered=*/false);
        body_->SetText(draft_.body);
        root_->AddChild(body_);
        body_->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    }

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
    std::ifstream is(path, std::ios::binary);
    if (!is) return false;
    Attachment a;
    a.filename  = PathToUtf8(PathFromUtf8(path).filename());
    a.mediaType = GuessMediaType(a.filename);
    a.data.assign(std::istreambuf_iterator<char>(is), std::istreambuf_iterator<char>());
    draft_.attachments.push_back(std::move(a));
    RefreshAttachments();
    return true;
}

std::shared_ptr<UltraCanvasContainer> ComposeView::BuildFormatRow() {
    auto row = CreateContainer("cFormat", 0, 0, 0, Theme::kControlHeight);
    row->layout.SetFlexRow()
               .SetFlexGap(4.0f)
               .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    ContainerStyle rowStyle;
    rowStyle.autoShowScrollbars = false;
    row->SetContainerStyle(rowStyle);

    auto add = [&](const std::string& id, const std::string& label, const std::string& tooltip,
                   std::function<void(UltraCanvasRichTextEdit&)> action) {
        auto button = CreateButton(id, 0, 0, 32, Theme::kControlHeight, label);
        Theme::FitToLabel(button, 32);
        Theme::StyleSecondary(button);
        button->SetTooltip(tooltip);
        // The editor is looked up on each click, never captured: the row
        // outlives neither the view nor its editor.
        button->onClick = [this, action]() {
            if (!rich_) return;
            action(*rich_);
            rich_->SetFocus(true);
        };
        row->AddChild(button);
    };
    add("cBold", "B", "Bold (Ctrl+B)", [](UltraCanvasRichTextEdit& e) { e.ToggleBold(); });
    add("cItalic", "I", "Italic (Ctrl+I)", [](UltraCanvasRichTextEdit& e) { e.ToggleItalic(); });
    add("cUnderline", "U", "Underline (Ctrl+U)", [](UltraCanvasRichTextEdit& e) { e.ToggleUnderline(); });
    add("cBullets", "\xE2\x80\xA2 List", "Bulleted list",
        [](UltraCanvasRichTextEdit& e) { e.ToggleBulletList(); });
    add("cNumbers", "1. List", "Numbered list",
        [](UltraCanvasRichTextEdit& e) { e.ToggleNumberedList(); });
    return row;
}

void ComposeView::InsertLink(const std::string& name, const std::string& url) {
    if (rich_) {
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
        options, [this](DialogResult result, const std::string& path) {
            if (result != DialogResult::OK || path.empty()) return;
            if (!AttachFile(path))
                UltraCanvasDialogManager::ShowError("Could not read " + path, "Attach file",
                                                    nullptr, parent_);
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
        [this](const UltraCloud::CloudLinkPick& pick) {
            InsertLink(pick.entry.name, pick.link.url);
        });
}

Draft ComposeView::CollectDraft() const {
    Draft d = draft_;   // keep from/identity, in-reply-to, references, attachments
    if (to_)      d.to = Split(to_->GetText());
    if (cc_)      d.cc = Split(cc_->GetText());
    if (subject_) d.subject = subject_->GetText();
    if (body_)    d.body = body_->GetText();
    if (rich_) {
        // The edited document goes out as HTML with a plain-text version.
        d.richBody = rich_->GetDocument();
        RenderRichBody(d);
    }
    return d;
}

} // namespace UltraMail
