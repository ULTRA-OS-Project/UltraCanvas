// Apps/UltraMail/ui/UltraMailComposeWindow.cpp
// Version: 0.1.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailComposeWindow.h"

#include "UltraCanvasButton.h"

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

std::string Trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b-1] == ' ' || s[b-1] == '\t')) --b;
    return s.substr(a, b - a);
}

std::vector<std::string> Split(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) {
        std::string t = Trim(item);
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

    body_ = std::make_shared<UltraCanvasTextArea>("cBody", 12, 124, 600, 360);
    body_->SetEditingMode(TextAreaEditingMode::PlainText);
    body_->SetText(draft_.body);
    root_->AddChild(body_);

    auto sendBtn = CreateButton("cSend", 12, 496, 120, 32, "Send");
    sendBtn->onClick = [this]() { if (onSend) onSend(CollectDraft()); };
    root_->AddChild(sendBtn);

    auto cancelBtn = CreateButton("cCancel", 140, 496, 120, 32, "Cancel");
    cancelBtn->onClick = [this]() { if (onCancel) onCancel(); };
    root_->AddChild(cancelBtn);

    return root_;
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
