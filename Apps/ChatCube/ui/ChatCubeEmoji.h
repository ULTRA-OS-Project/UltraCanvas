// Apps/ChatCube/ui/ChatCubeEmoji.h
// The smiley button's sets and its picker. A set is either smileys -
// emoji put into the message field at the caret - or stickers, pictures
// that go with the message like a pasted one. The arrow beside the smiley
// button lists the sets, each by one example of what it holds; choosing one
// opens its grid, and the smiley button opens the one chosen last.
//
// One sticker set comes with ChatCube as an example of what a set is:
// "Cubes", media/icons/chatcube/stickers/*.svg, drawn for ChatCube. A
// sticker is sent as a PNG of 256 x 256 pixels, written into ChatCube's
// cache, so every network shows it.
//
// Version: 0.1.0
// Last Modified: 2026-11-23
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasWindow.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {
class UltraCanvasContainer;
class UltraCanvasMenu;
}

namespace ChatCube {

struct EmojiSet {
    std::string key;                    // "smileys", ... - kept in the settings
    std::string name;                   // as the list shows it (translated)
    std::string example;                // the emoji the list shows it by ("" for stickers)
    bool stickers = false;
    std::vector<std::string> items;     // emoji, or sticker names (stickers/<name>.svg)
};

// Every set, the smileys first and the stickers last
const std::vector<EmojiSet>& EmojiSets();
// The set with that key; the first set for an unknown one
const EmojiSet& FindEmojiSet(const std::string& key);
// A sticker's drawing (UTF-8 path)
std::string StickerPath(const std::string& name);

class EmojiPicker {
public:
    EmojiPicker();
    ~EmojiPicker();

    // An emoji chosen: into the message field at the caret
    std::function<void(const std::string& emoji)> onEmoji;
    // A sticker chosen: its drawing (StickerPath)
    std::function<void(const std::string& path)> onSticker;
    // A set chosen in the list (to remember it)
    std::function<void(const std::string& key)> onSetChosen;

    // The grid of `setKey` just above `anchor` (the button, in window
    // coordinates), its left edge at the button's. Stays open for several
    // picks; a click outside or Escape closes it.
    void OpenGrid(UltraCanvas::UltraCanvasWindow& window, const UltraCanvas::Rect2Di& anchor,
                  const std::string& setKey);
    // The list of sets by the button, each with its example; choosing one
    // opens its grid at the same place
    void OpenSetList(UltraCanvas::UltraCanvasWindow& window, const UltraCanvas::Rect2Di& anchor);
    void Close();

private:
    UltraCanvas::UltraCanvasWindow* window_ = nullptr;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> grid_;
    std::shared_ptr<UltraCanvas::UltraCanvasMenu> list_;
};

} // namespace ChatCube
