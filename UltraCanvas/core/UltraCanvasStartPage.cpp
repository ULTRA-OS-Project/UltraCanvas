// UltraCanvas/core/UltraCanvasStartPage.cpp
// See include/UltraCanvasStartPage.h.
// Version: 1.0.0
// Author: UltraCanvas Framework
#include "UltraCanvasStartPage.h"

#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasTheme.h"

#include <algorithm>
#include <filesystem>

namespace UltraCanvas {

namespace {

const int kTitleHeight = 32;
const int kHintHeight = 70;    // three lines of 14 px: a path and what was wrong with it
const int kRow = 34;
const int kHeadingHeight = 24;

void Follow(const std::shared_ptr<UltraCanvasLabel>& label, LabelVariant variant) {
    label->SetThemeVariant(variant);
    label->SetFollowTheme(true);
}

void Follow(const std::shared_ptr<UltraCanvasButton>& button, ButtonVariant variant) {
    button->SetThemeVariant(variant);
    button->SetFollowTheme(true);
}

} // namespace

UltraCanvasStartPage::UltraCanvasStartPage(const std::string& identifier, int width)
    : UltraCanvasContainer(identifier, 0, 0, static_cast<float>(width), 0.0f), width_(width), y_(Margin()) {
    title_ = CreateLabel(identifier + ".title", Margin(), y_, ContentWidth(), kTitleHeight, "");
    title_->SetFontSize(22);
    title_->SetFontWeight(FontWeight::Bold);
    Follow(title_, LabelVariant::Heading);
    AddChild(title_);
    y_ += kTitleHeight + 8;
    hint_ = CreateLabel(identifier + ".hint", Margin(), y_, ContentWidth(), kHintHeight, "");
    hint_->SetFontSize(14);
    hint_->SetWrap(TextWrap::WrapWord);
    Follow(hint_, LabelVariant::Body);
    AddChild(hint_);
    y_ += kHintHeight + 14;
    Grow(0);
}

void UltraCanvasStartPage::Grow(int height) {
    y_ += height;
    SetSize(static_cast<float>(width_), static_cast<float>(y_ + Margin()));
}

void UltraCanvasStartPage::SetTitle(const std::string& text) { title_->SetText(text); }
void UltraCanvasStartPage::SetHint(const std::string& text) { hint_->SetText(text); }

void UltraCanvasStartPage::SetRecent(const std::string& heading, const std::vector<std::string>& entries,
                                     std::function<void(const std::string&)> onPick, int shown) {
    const int count = static_cast<int>(std::min<size_t>(entries.size(), static_cast<size_t>(std::max(shown, 0))));
    if (count == 0) return;
    auto title = CreateLabel(GetIdentifier() + ".recent", Margin(), y_, ContentWidth(), kHeadingHeight, heading);
    title->SetFontSize(15);
    title->SetFontWeight(FontWeight::Bold);
    Follow(title, LabelVariant::Heading);
    AddChild(title);
    Grow(kHeadingHeight + 6);
    for (int i = 0; i < count; ++i) {
        const std::string entry = entries[static_cast<size_t>(i)];
        const std::string last = PathToUtf8(PathFromUtf8(entry).filename());
        auto button = CreateButton(GetIdentifier() + ".recent" + std::to_string(i), Margin(), y_, ContentWidth(), kRow,
                                   (last.empty() ? entry : last) + "  \xE2\x80\x93  " + entry);
        button->SetBorder(1, Colors::Transparent);
        button->SetCornerRadius(6);
        button->SetTextAlign(TextAlignment::Left);
        button->SetPadding(0, 10);
        button->SetTooltip(entry);
        Follow(button, ButtonVariant::Standard);
        button->onClick = [onPick, entry]() { if (onPick) onPick(entry); };
        AddChild(button);
        Grow(kRow + 6);
    }
    Grow(10);
}

std::shared_ptr<UltraCanvasButton> UltraCanvasStartPage::AddAction(const std::string& text, std::function<void()> onClick,
                                                                   const std::string& note, bool primary) {
    // As wide as the column: a long label ("Bestehende Buchhaltung öffnen ...") fits, bold.
    auto button = CreateButton(GetIdentifier() + ".action" + std::to_string(next_++), Margin(), y_, ContentWidth(), kRow, text);
    {
        ButtonStyle style = button->GetStyle();
        style.fontSize = 14.0f;
        if (primary) style.fontWeight = FontWeight::Bold;
        button->SetStyle(style);
    }
    button->SetBorder(primary ? 0 : 1, Colors::Transparent);
    button->SetCornerRadius(6);
    Follow(button, primary ? ButtonVariant::Primary : ButtonVariant::Standard);
    button->onClick = std::move(onClick);
    AddChild(button);
    Grow(kRow + 8);
    if (!note.empty()) {
        auto label = CreateLabel(GetIdentifier() + ".note" + std::to_string(next_++), Margin(), y_, ContentWidth(), 40, note);
        label->SetFontSize(12);
        label->SetWrap(TextWrap::WrapWord);
        Follow(label, LabelVariant::Muted);
        AddChild(label);
        Grow(40);
    }
    Grow(8);
    return button;
}

std::shared_ptr<UltraCanvasContainer> UltraCanvasStartPage::AddSection(const std::string& heading, int height) {
    auto title = CreateLabel(GetIdentifier() + ".section" + std::to_string(next_++), Margin(), y_, ContentWidth(), kHeadingHeight, heading);
    title->SetFontSize(15);
    title->SetFontWeight(FontWeight::Bold);
    Follow(title, LabelVariant::Heading);
    AddChild(title);
    Grow(kHeadingHeight + 6);
    auto section = CreateContainer(GetIdentifier() + ".fields" + std::to_string(next_++), Margin(), y_, ContentWidth(), height);
    AddChild(section);
    Grow(height + 8);
    return section;
}

std::shared_ptr<UltraCanvasLabel> UltraCanvasStartPage::AddStatusLine() {
    status_ = CreateLabel(GetIdentifier() + ".status", Margin(), y_, ContentWidth(), 44, "");
    status_->SetFontSize(13);
    status_->SetWrap(TextWrap::WrapWord);
    Follow(status_, LabelVariant::Muted);
    AddChild(status_);
    Grow(44);
    return status_;
}

void UltraCanvasStartPage::Say(const std::string& text, bool error) {
    if (!status_) AddStatusLine();
    if (error) {
        status_->SetFollowTheme(false);
        status_->SetTextColor(UltraCanvasTheme::Current().roles.danger);
    } else {
        Follow(status_, LabelVariant::Muted);
    }
    status_->SetText(text);
}

int UltraCanvasStartPage::PreferredHeight() const { return y_ + Margin(); }

} // namespace UltraCanvas
