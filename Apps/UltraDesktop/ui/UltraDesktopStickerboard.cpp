// Apps/UltraDesktop/ui/UltraDesktopStickerboard.cpp
// Sticky notes over the wallpaper. See the header.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraDesktopStickerboard.h"
#include "UltraDesktopWindow.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextArea.h"

#include <algorithm>
#include <cstdlib>

using namespace UltraCanvas;

namespace UltraDesktop {
namespace {

constexpr float kGripHeight = 24;
constexpr float kNoteMinWidth = 120;
constexpr float kNoteMinHeight = 80;
constexpr float kAddButton = 36;

// The papers a note can wear, in the order the colour button cycles them.
const char* const kPapers[] = {"#FFF59D", "#C8E6C9", "#BBDEFB", "#F8BBD0", "#FFE0B2", "#E1BEE7"};

Color PaperColor(const std::string& hex) {
    // "#RRGGBB"; anything else is the first paper.
    if (hex.size() == 7 && hex[0] == '#') {
        auto channel = [&hex](size_t at) -> int {
            return static_cast<int>(std::strtol(hex.substr(at, 2).c_str(), nullptr, 16));
        };
        return Color(static_cast<uint8_t>(channel(1)), static_cast<uint8_t>(channel(3)),
                     static_cast<uint8_t>(channel(5)), 255);
    }
    return Color(255, 245, 157, 255);
}

std::string NextPaper(const std::string& current) {
    const size_t count = sizeof(kPapers) / sizeof(kPapers[0]);
    for (size_t i = 0; i < count; ++i) {
        if (current == kPapers[i]) return kPapers[(i + 1) % count];
    }
    return kPapers[0];
}

} // namespace

UltraDesktopStickerboard::UltraDesktopStickerboard(const std::string& identifier, UltraDesktopWindow* desktop)
    : UltraCanvasContainer(identifier, 0, 0, 0, 0), desktop_(desktop) {
    SetBackgroundColor(Colors::Transparent);
    ContainerStyle plain;
    plain.autoShowScrollbars = false;
    SetContainerStyle(plain);

    // The "+" in the board's top-left corner.
    auto add = std::make_shared<UltraCanvasButton>("Stickerboard.Add", 12, 12, kAddButton, kAddButton, "+");
    ButtonStyle style = add->GetStyle();
    style.normalColor = Color(255, 245, 157, 230);
    style.hoverColor = Color(255, 235, 120, 255);
    style.pressedColor = Color(240, 220, 90, 255);
    style.borderWidth = 0;
    style.cornerRadius = kAddButton / 2;
    style.fontSize = 20;
    add->SetStyle(style);
    add->SetTooltip("New note");
    add->onClick = [this]() { AddNote(); };
    PlaceChildAt(add, Rect2Df(12, 12, kAddButton, kAddButton));
    addButton_ = add;
}

void UltraDesktopStickerboard::LoadFrom(const DesktopSettings& settings) {
    for (auto& [id, note] : notes_) RemoveChild(note.card);
    notes_.clear();
    for (const Sticker& s : settings.stickers) BuildNote(s);
    InvalidateLayout();
    RequestRedraw();
}

void UltraDesktopStickerboard::AddNote() {
    if (!desktop_) return;
    DesktopSettings& settings = desktop_->Settings();
    Sticker s;
    s.id = settings.NewStickerId();
    // Cascade new notes so a second one is not hidden behind the first.
    const float offset = 24.0f * static_cast<float>(settings.stickers.size() % 8);
    s.x = 60 + offset;
    s.y = 60 + offset;
    settings.stickers.push_back(s);
    BuildNote(s);
    Persist();
    InvalidateLayout();
    RequestRedraw();
    auto found = notes_.find(s.id);
    if (found != notes_.end() && found->second.text) found->second.text->SetFocus(true);
}

void UltraDesktopStickerboard::BuildNote(const Sticker& sticker) {
    Note note;
    const std::string& id = sticker.id;
    const Color paper = PaperColor(sticker.color);

    note.card = CreateContainer("Sticker." + id, 0, 0, sticker.width, sticker.height);
    note.card->SetBackgroundColor(paper);
    note.card->SetBorders(1, Color(0, 0, 0, 40), 6);
    note.card->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    ContainerStyle plain;
    plain.autoShowScrollbars = false;
    note.card->SetContainerStyle(plain);

    // The bar: drag here; colour and close on the right.
    auto bar = CreateContainer("Sticker." + id + ".Bar", 0, 0, 0, kGripHeight);
    bar->SetBackgroundColor(Color(0, 0, 0, 25));
    bar->layout.SetFlexRow().SetFlexAlignItems(CSSLayout::AlignItems::Center).SetFlexGap(2);
    bar->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(kGripHeight));
    bar->layoutItem.SetFlexShrink(0.0f);
    bar->SetContainerStyle(plain);

    note.grip = CreateLabel("Sticker." + id + ".Grip", "⋯");
    note.grip->SetTextColor(Color(0, 0, 0, 120));
    note.grip->SetAlignment(TextAlignment::Center);
    note.grip->layoutItem.SetFlexGrow(1.0f);
    note.grip->SetTooltip("Drag to move");
    bar->AddChild(note.grip);

    auto colour = std::make_shared<UltraCanvasButton>("Sticker." + id + ".Color", 0, 0, 22, 20, "◐");
    auto close = std::make_shared<UltraCanvasButton>("Sticker." + id + ".Close", 0, 0, 22, 20, "×");
    for (auto& b : {colour, close}) {
        ButtonStyle style = b->GetStyle();
        style.normalColor = Colors::Transparent;
        style.hoverColor = Color(0, 0, 0, 40);
        style.pressedColor = Color(0, 0, 0, 70);
        style.borderWidth = 0;
        style.fontSize = 13;
        b->SetStyle(style);
        b->layoutItem.SetFlexShrink(0.0f);
    }
    colour->SetTooltip("Next colour");
    colour->onClick = [this, id]() { CycleColor(id); };
    close->SetTooltip("Remove note");
    close->onClick = [this, id]() { RemoveNote(id); };
    bar->AddChild(colour);
    bar->AddChild(close);
    note.card->AddChild(bar);

    note.text = std::make_shared<UltraCanvasTextArea>("Sticker." + id + ".Text", 0, 0, 0, 0);
    note.text->SetText(sticker.text, /*runNotifications=*/false);
    note.text->SetWordWrap(true);
    note.text->SetShowLineNumbers(false);
    TextAreaStyle textStyle = note.text->GetStyle();
    textStyle.backgroundColor = paper;
    textStyle.borderColor = Colors::Transparent;
    textStyle.fontColor = Color(40, 40, 40, 255);
    note.text->SetStyle(textStyle);
    note.text->SetOnTextChanged([this, id](const std::string& text) { SaveNoteText(id, text); });
    note.text->layoutItem.SetFlexGrow(1.0f).SetFlexShrink(1.0f).SetFlexBasis(CSSLayout::Dimension::Px(0));
    note.text->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    note.card->AddChild(note.text);

    notes_[id] = note;
    PlaceNote(id, sticker);
}

void UltraDesktopStickerboard::PlaceNote(const std::string& id, const Sticker& sticker) {
    auto found = notes_.find(id);
    if (found == notes_.end()) return;
    PlaceChildAt(found->second.card, Rect2Df(sticker.x, sticker.y,
                                             std::max(kNoteMinWidth, sticker.width),
                                             std::max(kNoteMinHeight, sticker.height)));
}

void UltraDesktopStickerboard::RemoveNote(const std::string& id) {
    auto found = notes_.find(id);
    if (found == notes_.end()) return;
    RemoveChild(found->second.card);
    notes_.erase(found);
    if (desktop_) desktop_->Settings().RemoveSticker(id);
    Persist();
    InvalidateLayout();
    RequestRedraw();
}

void UltraDesktopStickerboard::CycleColor(const std::string& id) {
    if (!desktop_) return;
    Sticker* s = desktop_->Settings().FindSticker(id);
    auto found = notes_.find(id);
    if (!s || found == notes_.end()) return;
    s->color = NextPaper(s->color);
    const Color paper = PaperColor(s->color);
    found->second.card->SetBackgroundColor(paper);
    TextAreaStyle textStyle = found->second.text->GetStyle();
    textStyle.backgroundColor = paper;
    found->second.text->SetStyle(textStyle);
    Persist();
    RequestRedraw();
}

void UltraDesktopStickerboard::SaveNoteText(const std::string& id, const std::string& text) {
    if (!desktop_) return;
    if (Sticker* s = desktop_->Settings().FindSticker(id)) {
        s->text = text;
        Persist();
    }
}

void UltraDesktopStickerboard::Persist() {
    if (desktop_) desktop_->SaveSettings();
}

bool UltraDesktopStickerboard::Contains(const Point2Df& point) {
    if (!IsVisible()) return false;
    if (addButton_ && addButton_->GetBounds().Contains(point)) return true;
    for (const auto& [id, note] : notes_) {
        if (note.card->GetBounds().Contains(point)) return true;
    }
    return false;
}

bool UltraDesktopStickerboard::OnEvent(const UCEvent& event) {
    // Dragging a note by its grip: the press lands on the grip label (it
    // takes no events, so it bubbles here), the moves follow the pointer,
    // the release writes the place into the settings.
    auto* app = UltraCanvasApplication::GetInstance();
    switch (event.type) {
        case UCEventType::MouseDown:
            if (event.button == UCMouseButton::Left) {
                for (const auto& [id, note] : notes_) {
                    const Rect2Df card = note.card->GetBounds();
                    const Rect2Df grip(card.x, card.y, card.width - 48, kGripHeight);
                    if (grip.Contains(event.pointer)) {
                        draggingId_ = id;
                        dragOffset_ = Point2Df(event.pointer.x - card.x, event.pointer.y - card.y);
                        if (app) app->CaptureMouse(this);
                        return true;
                    }
                }
            }
            break;
        case UCEventType::MouseMove:
            if (!draggingId_.empty() && desktop_) {
                if (Sticker* s = desktop_->Settings().FindSticker(draggingId_)) {
                    s->x = std::max(0.0f, event.pointer.x - dragOffset_.x);
                    s->y = std::max(0.0f, event.pointer.y - dragOffset_.y);
                    PlaceNote(draggingId_, *s);
                    InvalidateLayout();
                    RequestRedraw();
                }
                return true;
            }
            break;
        case UCEventType::MouseUp:
            if (!draggingId_.empty()) {
                draggingId_.clear();
                if (app) app->ReleaseMouse();
                Persist();
                return true;
            }
            break;
        default:
            break;
    }
    return UltraCanvasContainer::OnEvent(event);
}

void UltraDesktopStickerboard::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
    UltraCanvasContainer::Render(ctx, dirtyRect);
}

} // namespace UltraDesktop
