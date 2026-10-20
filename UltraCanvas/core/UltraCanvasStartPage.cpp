// UltraCanvas/core/UltraCanvasStartPage.cpp
// See include/UltraCanvasStartPage.h.
// Version: 1.1.1 - a recent entry too long for its button loses its middle, not its end
// Version: 1.1.0 - AddField, AddNote and AddButton; a section grows with its rows
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
const int kRow = UltraCanvasStartPage::RowHeight();
const int kRowGap = 10;
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
    // The layout's size, not the final bounds alone: the layout pass would
    // put those back.
    SetElementSize(Size2Df(static_cast<float>(width_), static_cast<float>(y_ + Margin())));
}

int UltraCanvasStartPage::GrowSection(int height) {
    if (!section_) return -1;
    // The next row goes under the last one, with a gap between rows.
    const int y = sectionHeight_ == 0 ? 0 : sectionHeight_ + kRowGap;
    sectionHeight_ = y + height;
    section_->SetElementSize(Size2Df(static_cast<float>(ContentWidth()), static_cast<float>(sectionHeight_)));
    // The page's cursor sits 8 px under the section; it moves with it.
    y_ = static_cast<int>(section_->GetY()) + sectionHeight_;
    Grow(8);
    return y;
}

void UltraCanvasStartPage::SetTitle(const std::string& text) { title_->SetText(text); }
void UltraCanvasStartPage::SetHint(const std::string& text) { hint_->SetText(text); }

void UltraCanvasStartPage::SetRecent(const std::string& heading, const std::vector<std::string>& entries,
                                     std::function<void(const std::string&)> onPick, int shown) {
    const int count = static_cast<int>(std::min<size_t>(entries.size(), static_cast<size_t>(std::max(shown, 0))));
    if (count == 0) return;
    section_.reset();   // a section is the last part only until the next
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
        // A path too long for the button loses its middle, so the last part
        // and the drive or root stay readable; the tooltip has it whole.
        button->SetEllipsize(EllipsizeMode::EllipsizeMiddle);
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
    section_.reset();   // a section is the last part only until the next
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
    section_.reset();
    auto title = CreateLabel(GetIdentifier() + ".section" + std::to_string(next_++), Margin(), y_, ContentWidth(), kHeadingHeight, heading);
    title->SetFontSize(15);
    title->SetFontWeight(FontWeight::Bold);
    Follow(title, LabelVariant::Heading);
    AddChild(title);
    Grow(kHeadingHeight + 6);
    auto section = CreateContainer(GetIdentifier() + ".fields" + std::to_string(next_++), Margin(), y_, ContentWidth(), height);
    AddChild(section);
    Grow(height + 8);
    if (height == 0) {
        section_ = section;
        sectionHeight_ = 0;
    }
    return section;
}

int UltraCanvasStartPage::AddField(const std::string& caption, const std::shared_ptr<UltraCanvasUIElement>& input,
                                   int captionWidth) {
    if (!section_ || !input) return -1;
    // The input keeps a size of its own (a date picker, a dropdown); one
    // without gets the rest of the column, a row tall.
    const bool ownSize = input->GetWidth() > 0;
    const int height = ownSize ? std::max(static_cast<int>(input->GetHeight()), kRow) : kRow;
    const int y = GrowSection(height);
    auto label = CreateLabel(GetIdentifier() + ".caption" + std::to_string(next_++), 0, y, captionWidth, height, caption);
    label->SetFontSize(13);
    Follow(label, LabelVariant::Body);
    section_->AddChild(label);
    input->SetElementAbsolutePosition(Point2Df(static_cast<float>(captionWidth), static_cast<float>(y)));
    if (!ownSize) input->SetElementSize(Size2Df(static_cast<float>(ContentWidth() - captionWidth), static_cast<float>(kRow)));
    section_->AddChild(input);
    return y;
}

std::shared_ptr<UltraCanvasLabel> UltraCanvasStartPage::AddNote(const std::string& text, int height) {
    if (!section_) return nullptr;
    const int y = GrowSection(height);
    auto label = CreateLabel(GetIdentifier() + ".note" + std::to_string(next_++), 0, y, ContentWidth(), height, text);
    label->SetFontSize(12);
    label->SetWrap(TextWrap::WrapWord);
    Follow(label, LabelVariant::Muted);
    section_->AddChild(label);
    return label;
}

std::shared_ptr<UltraCanvasButton> UltraCanvasStartPage::AddButton(const std::string& text, std::function<void()> onClick,
                                                                   int width) {
    if (!section_) return nullptr;
    const int y = GrowSection(kRow);
    auto button = CreateButton(GetIdentifier() + ".button" + std::to_string(next_++), 0, y,
                               width > 0 ? std::min(width, ContentWidth()) : ContentWidth(), kRow, text);
    {
        ButtonStyle style = button->GetStyle();
        style.fontSize = 14.0f;
        button->SetStyle(style);
    }
    button->SetBorder(1, Colors::Transparent);
    button->SetCornerRadius(6);
    Follow(button, ButtonVariant::Standard);
    button->onClick = std::move(onClick);
    section_->AddChild(button);
    return button;
}

std::shared_ptr<UltraCanvasLabel> UltraCanvasStartPage::AddStatusLine() {
    section_.reset();   // the section is no longer the last part
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
