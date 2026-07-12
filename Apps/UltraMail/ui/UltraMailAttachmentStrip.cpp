// Apps/UltraMail/ui/UltraMailAttachmentStrip.cpp
// Version: 0.1.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailAttachmentStrip.h"

#include "UltraCanvasLabel.h"
#include "UltraCanvasEvent.h"

#include <cstdio>
#include <string>

using namespace UltraCanvas;

namespace UltraMail {

namespace {

constexpr float kChipW = 190.0f;
constexpr float kChipH = 44.0f;
constexpr float kGap   = 8.0f;

// A glyph hinting at the attachment kind.
std::string GlyphFor(const std::string& mediaType) {
    if (mediaType.rfind("image/", 0) == 0) return "🖼";
    if (mediaType.rfind("audio/", 0) == 0) return "🎵";
    if (mediaType.rfind("video/", 0) == 0) return "🎬";
    if (mediaType == "application/pdf")    return "📄";
    if (mediaType.rfind("text/", 0) == 0)  return "📃";
    return "📎";
}

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
    AddChild(CreateLabel(id + ".glyph", 6, 4, 28, 28, GlyphFor(att.mediaType)));

    std::string name = att.filename.empty() ? "attachment" : att.filename;
    AddChild(CreateLabel(id + ".name", 38, 4, w - 44, 20, name));
    AddChild(CreateLabel(id + ".size", 38, 24, w - 44, 16, HumanSize(att.Size())));
}

bool AttachmentChip::OnEvent(const UCEvent& event) {
    if (!IsVisible() || IsDisabled()) return false;

    if (event.type == UCEventType::MouseDoubleClick) {
        if (onOpen_) onOpen_();
        return true;
    }
    if (event.type == UCEventType::MouseDown && event.button == UCMouseButton::Right) {
        ShowContextMenu(event);
        return true;
    }
    return UltraCanvasContainer::OnEvent(event);
}

void AttachmentChip::ShowContextMenu(const UCEvent& event) {
    UltraCanvasWindowBase* window = GetWindow();
    if (!window) return;

    menu_ = std::make_shared<UltraCanvasMenu>(GetIdentifier() + ".ctx", 0, 0, 180, 0);
    menu_->SetMenuType(MenuType::PopupMenu);
    menu_->AddItem(MenuItemData::Action("Open", [this]() { if (onOpen_) onOpen_(); }));
    menu_->AddItem(MenuItemData::Action("Save As…", [this]() { if (onSaveAs_) onSaveAs_(); }));

    PopupElementSettings settings;
    menu_->OpenMenu(event.pointerWindow, *window, settings);
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
