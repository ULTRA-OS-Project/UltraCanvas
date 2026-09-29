// Apps/UltraDesktop/ui/UltraDesktopStickerboard.h
// The Stickerboard: sticky notes over the wallpaper. Each note is an
// UltraCanvasTextArea on a coloured card with a small bar to drag it by,
// a colour to cycle and a close button; the board itself is an
// UltraCanvasContainer laid over the work area, with one "+" to add a note.
// The notes' text, place and colour live in the desktop settings, so they
// come back where they were.
//
// The board paints no control of its own: the text is the framework's text
// area (caret, selection, clipboard, undo, IME all come with it), the
// buttons are framework buttons, the drag bar is a label that the board
// moves on a press-and-drag.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraDesktopSettings.h"

#include "UltraCanvasContainer.h"

#include <map>
#include <memory>
#include <string>

namespace UltraCanvas {
    class UltraCanvasTextArea;
    class UltraCanvasLabel;
}

namespace UltraDesktop {

class UltraDesktopWindow;

class UltraDesktopStickerboard : public UltraCanvas::UltraCanvasContainer {
public:
    UltraDesktopStickerboard(const std::string& identifier, UltraDesktopWindow* desktop);

    // Rebuild the notes from the settings' list.
    void LoadFrom(const DesktopSettings& settings);
    void AddNote();

    bool OnEvent(const UltraCanvas::UCEvent& event) override;
    void Render(UltraCanvas::IRenderContext* ctx, const UltraCanvas::Rect2Df& dirtyRect) override;
    // The board only answers where a note is: the wallpaper's empty space
    // belongs to the application windows floating above.
    bool Contains(const UltraCanvas::Point2Df& point) override;

private:
    struct Note {
        std::shared_ptr<UltraCanvasContainer> card;
        std::shared_ptr<UltraCanvas::UltraCanvasLabel> grip;
        std::shared_ptr<UltraCanvas::UltraCanvasTextArea> text;
    };

    void BuildNote(const Sticker& sticker);
    void PlaceNote(const std::string& id, const Sticker& sticker);
    void RemoveNote(const std::string& id);
    void CycleColor(const std::string& id);
    void SaveNoteText(const std::string& id, const std::string& text);
    void Persist();

    UltraDesktopWindow* desktop_;   // owns this board through the window tree
    std::map<std::string, Note> notes_;
    std::shared_ptr<UltraCanvas::UltraCanvasUIElement> addButton_;

    // Dragging a note by its grip.
    std::string draggingId_;
    UltraCanvas::Point2Df dragOffset_;
};

} // namespace UltraDesktop
