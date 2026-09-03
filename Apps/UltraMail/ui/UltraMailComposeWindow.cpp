// Apps/UltraMail/ui/UltraMailComposeWindow.cpp
// Version: 0.3.0
// Last Modified: 2026-09-03
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailComposeWindow.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasModalDialog.h"

#include "UltraCloudPickerDialog.h"

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

} // namespace

std::shared_ptr<UltraCanvasContainer> ComposeView::Build() {
    root_ = CreateContainer("composeView", 0, 0, 0, 0);

    root_->AddChild(CreateLabel("cLblTo", 12, 14, 70, 24, "To"));
    to_ = CreateTextInput("cTo", 90, 12, 520, 28);
    to_->SetText(Join(draft_.to));
    to_->SetPlaceholder("recipient@example.com, …");
    root_->AddChild(to_);

    root_->AddChild(CreateLabel("cLblCc", 12, 50, 70, 24, "Cc"));
    cc_ = CreateTextInput("cCc", 90, 48, 520, 28);
    cc_->SetText(Join(draft_.cc));
    root_->AddChild(cc_);

    root_->AddChild(CreateLabel("cLblSubj", 12, 86, 70, 24, "Subject"));
    subject_ = CreateTextInput("cSubj", 90, 84, 520, 28);
    subject_->SetText(draft_.subject);
    root_->AddChild(subject_);

    body_ = std::make_shared<UltraCanvasTextArea>("cBody", 12, 124, 600, 316);
    body_->SetEditingMode(TextAreaEditingMode::PlainText);
    body_->SetText(draft_.body);
    root_->AddChild(body_);

    // Attachment chips between the body and the buttons (empty until a file
    // is attached; forwards carry the original's attachments).
    auto strip = CreateContainer("cAttachWrap", 12, 442, 600, 52);
    ContainerStyle stripStyle;
    stripStyle.autoShowScrollbars = false;
    strip->SetContainerStyle(stripStyle);
    strip->AddChild(attachments_.Build());
    root_->AddChild(strip);
    attachments_.SetAttachments(draft_.attachments);

    auto sendBtn = CreateButton("cSend", 12, 496, 120, 32, "Send");
    sendBtn->onClick = [this]() { if (onSend) onSend(CollectDraft()); };
    root_->AddChild(sendBtn);

    auto cancelBtn = CreateButton("cCancel", 140, 496, 120, 32, "Cancel");
    cancelBtn->onClick = [this]() { if (onCancel) onCancel(); };
    root_->AddChild(cancelBtn);

    auto attachBtn = CreateButton("cAttach", 300, 496, 130, 32, "Attach file…");
    attachBtn->onClick = [this]() { ChooseFileToAttach(); };
    root_->AddChild(attachBtn);

    auto cloudBtn = CreateButton("cCloud", 438, 496, 174, 32, "Attach cloud link…");
    cloudBtn->onClick = [this]() { ChooseCloudLink(); };
    root_->AddChild(cloudBtn);

    return root_;
}

bool ComposeView::AttachFile(const std::string& path) {
    std::ifstream is(path, std::ios::binary);
    if (!is) return false;
    Attachment a;
    a.filename  = std::filesystem::path(path).filename().string();
    a.mediaType = GuessMediaType(a.filename);
    a.data.assign(std::istreambuf_iterator<char>(is), std::istreambuf_iterator<char>());
    draft_.attachments.push_back(std::move(a));
    attachments_.SetAttachments(draft_.attachments);
    return true;
}

void ComposeView::InsertLink(const std::string& name, const std::string& url) {
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
    return d;
}

} // namespace UltraMail
