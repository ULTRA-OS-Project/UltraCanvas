// Apps/UltraMail/ui/UltraMailAttachmentStrip.h
// The attachment strip shown under a message: one chip per attachment (the
// paperclip, the filename and the size). A click on a chip opens its menu
// (Open / Save As…) under the chip, a second click closes it again, and a
// double-click opens the attachment straight away; a right-click raises the
// same menu at the pointer. The actual viewing is delegated via callbacks so
// the strip stays presentation-only.
// Version: 0.3.0 - the paperclip instead of a type emoji (fonts without
//                  colour emoji drew a box); one click opens the menu
// Version: 0.1.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailMimeCodec.h"   // Attachment

#include "UltraCanvasContainer.h"
#include "UltraCanvasMenu.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraMail {

// One clickable attachment chip. Handles its clicks itself.
class AttachmentChip : public UltraCanvas::UltraCanvasContainer {
public:
    AttachmentChip(const std::string& id, float x, float y, float w, float h,
                   const Attachment& attachment,
                   std::function<void()> onOpen,
                   std::function<void()> onSaveAs);

    bool OnEvent(const UltraCanvas::UCEvent& event) override;

private:
    // The Open / Save As… menu: under the chip (above it when the window
    // has no room below), or at `atPointer` for a right-click.
    void ShowMenu(const UltraCanvas::UCEvent* atPointer);
    void CloseMenu();
    bool MenuOpen() const;

    std::function<void()> onOpen_;
    std::function<void()> onSaveAs_;
    std::shared_ptr<UltraCanvas::UltraCanvasMenu> menu_;
};

// The horizontal strip of chips.
class AttachmentStrip {
public:
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Build();

    // Replace the displayed attachments (rebuilds the chips).
    void SetAttachments(std::vector<Attachment> attachments);

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Container() const { return strip_; }

    std::function<void(const Attachment&)> onOpen;
    std::function<void(const Attachment&)> onSaveAs;

private:
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> strip_;
    std::shared_ptr<std::vector<Attachment>> attachments_ =
        std::make_shared<std::vector<Attachment>>();
};

} // namespace UltraMail
