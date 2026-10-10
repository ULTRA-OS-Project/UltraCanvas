// Apps/UltraMail/ui/UltraMailAttachmentStrip.cpp
// Version: 0.3.0 - the chip shows the paperclip (an UltraCanvasImageElement)
//                  instead of a type emoji, which a font without colour emoji
//                  drew as a box; a click opens the chip's menu under it, a
//                  double-click still opens the attachment
// Version: 0.2.1 - the strip is hidden while the message has no attachments
// Last Modified: 2026-10-10
// V0.2.0: chips are themed cards
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailAttachmentStrip.h"

#include "UltraMailTheme.h"

#include "UltraCanvasConfig.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasWindow.h"

#include <algorithm>
#include <cstdio>
#include <string>

using namespace UltraCanvas;

namespace UltraMail {

namespace {

constexpr float kChipW = 160.0f;
constexpr float kChipH = 34.0f;
constexpr float kGap   = 6.0f;
constexpr float kIcon  = 16.0f;
constexpr int   kMenuW = 180;

std::string HumanSize(std::size_t bytes) {
    char buf[32];
    if (bytes < 1024) std::snprintf(buf, sizeof buf, "%zu B", bytes);
    else if (bytes < 1024 * 1024) std::snprintf(buf, sizeof buf, "%.1f KB", bytes / 1024.0);
    else std::snprintf(buf, sizeof buf, "%.1f MB", bytes / (1024.0 * 1024.0));
    return buf;
}

} // namespace

// ---- AttachmentChip --------------------------------------------------------

AttachmentChip::AttachmentChip(const std::string& id, float x, float y, float w, float h,
                               const Attachment& att,
                               std::function<void()> onOpen,
                               std::function<void()> onSaveAs)
    : UltraCanvasContainer(id, x, y, w, h),
      onOpen_(std::move(onOpen)), onSaveAs_(std::move(onSaveAs)) {
    // A small white card with the paperclip, the name and the size.
    SetShowVerticalScrollbar(false);
    SetShowHorizontalScrollbar(false);
    SetBackgroundColor(Theme::kCardBackground);
    SetBorders(1.0f, Theme::kCardBorder, 8.0f);
    // It is clicked, like a button.
    SetMouseCursor(UCMouseCursor::Hand);

    // The chip handles clicks itself; its icon and labels are purely
    // presentational. Marking them non-interactive keeps hit-testing from
    // returning one of them, so every pointer event over the chip reaches
    // AttachmentChip::OnEvent.
    std::string name = att.filename.empty() ? "attachment" : att.filename;
    SetTooltip(name);

    // The paperclip the message list marks mail with attachments with. A
    // type emoji used to stand here, and a font without colour emoji (the
    // usual case on Linux and Windows) drew it as a box of lines.
    auto clip = CreateImageElement(id + ".clip", 9, (h - kIcon) / 2, kIcon, kIcon);
    clip->LoadFromFile(NormalizePath(GetResourcesDir() + "media/icons/paperclip.svg"));
    clip->SetInteractive(false);
    AddChild(clip);

    auto nameLabel = CreateLabel(id + ".name", 32, 3, w - 38, 14, name);
    nameLabel->SetFontSize(Theme::kSizeBody);
    nameLabel->SetTextColor(Theme::kTextPrimary);
    nameLabel->SetInteractive(false);
    AddChild(nameLabel);
    auto sizeLabel = CreateLabel(id + ".size", 32, 17, w - 38, 13, HumanSize(att.Size()));
    sizeLabel->SetFontSize(Theme::kSizeSmall);
    sizeLabel->SetTextColor(Theme::kTextMuted);
    sizeLabel->SetInteractive(false);
    AddChild(sizeLabel);
}

bool AttachmentChip::OnEvent(const UCEvent& event) {
    if (!IsVisible() || IsDisabled()) return false;

    // A double-click opens the attachment. Its first click has opened the
    // menu, which goes again: the chip is the menu's owner, so the second
    // click reached the chip and not the menu.
    if (event.type == UCEventType::MouseDoubleClick && event.button == UCMouseButton::Left) {
        CloseMenu();
        if (onOpen_) onOpen_();
        return true;
    }
    if (event.type == UCEventType::MouseDown && event.button == UCMouseButton::Left) {
        // A click opens the menu under the chip; a click while it is open
        // closes it, as a dropdown does.
        if (MenuOpen()) CloseMenu();
        else ShowMenu(nullptr);
        return true;
    }
    if (event.type == UCEventType::MouseDown && event.button == UCMouseButton::Right) {
        CloseMenu();
        ShowMenu(&event);
        return true;
    }
    return UltraCanvasContainer::OnEvent(event);
}

bool AttachmentChip::MenuOpen() const {
    return menu_ && menu_->GetWindow() != nullptr;
}

void AttachmentChip::CloseMenu() {
    if (!menu_) return;
    if (UltraCanvasWindowBase* window = menu_->GetWindow())
        window->ClosePopup(*menu_, ClosePopupReason::Manual);
    menu_.reset();
}

void AttachmentChip::ShowMenu(const UCEvent* atPointer) {
    UltraCanvasWindowBase* window = GetWindow();
    if (!window) return;

    menu_ = std::make_shared<UltraCanvasMenu>(GetIdentifier() + ".ctx", 0, 0, kMenuW, 0);
    menu_->SetMenuType(MenuType::PopupMenu);
    menu_->AddItem(MenuItemData::Action("Open", [this]() { if (onOpen_) onOpen_(); }));
    menu_->AddItem(MenuItemData::Action("Save As…", [this]() { if (onSaveAs_) onSaveAs_(); }));

    PopupElementSettings settings;
    // Clicks on the chip still reach the chip while the menu is up: the
    // second click of a double-click, or the click that closes the menu.
    settings.popupOwner = weak_from_this();

    Point2Di at;
    if (atPointer) {
        at = atPointer->pointerWindow;
    } else {
        // Under the chip, its left edges lined up. The chips sit at the foot
        // of the window, where the menu does not fit below: then it opens
        // above the chip, ending just over it. (Given the chip's top, the
        // menu would open downwards from there whenever it fits below that
        // point - over the chip, where the second click of a double-click
        // would land on Open.) A vertical popup is as tall as its items.
        const int menuH = 2 * menu_->GetStyle().itemHeight;
        constexpr int kMenuGap = 2;
        const int left = static_cast<int>(GetXInWindow());
        const int top = static_cast<int>(GetYInWindow());
        const int bottom = top + static_cast<int>(GetHeight());
        const bool roomBelow = bottom + kMenuGap + menuH <= window->GetHeight();
        at = Point2Di(left, roomBelow ? bottom + kMenuGap
                                      : std::max(0, top - kMenuGap - menuH));
    }
    menu_->OpenMenu(at, *window, settings);
}

// ---- AttachmentStrip -------------------------------------------------------

std::shared_ptr<UltraCanvasContainer> AttachmentStrip::Build() {
    strip_ = CreateContainer("attachmentStrip", 0, 0, 0, kChipH + 8);
    return strip_;
}

void AttachmentStrip::SetAttachments(std::vector<Attachment> attachments) {
    if (!strip_) Build();
    strip_->ClearChildren();
    *attachments_ = std::move(attachments);
    // Hidden (out of the layout) while there is nothing to show: an empty
    // strip kept its 42 px and the column's gap under every message, and
    // the body above it ended that far short of the pane's bottom.
    strip_->SetVisible(!attachments_->empty());

    float x = kGap;
    for (std::size_t i = 0; i < attachments_->size(); ++i) {
        auto openCb = [this, i]() { if (onOpen)   onOpen((*attachments_)[i]); };
        auto saveCb = [this, i]() { if (onSaveAs) onSaveAs((*attachments_)[i]); };
        auto chip = std::make_shared<AttachmentChip>(
            "att_" + std::to_string(i), x, 4, kChipW, kChipH,
            (*attachments_)[i], std::move(openCb), std::move(saveCb));
        strip_->AddChild(chip);
        x += kChipW + kGap;
    }
}

} // namespace UltraMail
