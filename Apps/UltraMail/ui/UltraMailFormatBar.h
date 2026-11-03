// Apps/UltraMail/ui/UltraMailFormatBar.h
// The formatting toolbar for an UltraCanvasRichTextEdit, shared by the
// compose window and the signature editor: icon-only tools on a tinted strip
// that the host puts on top of the editor. Two rows, or one
// (Options::singleRow):
//   * characters: bold, italic, underline, strikethrough | font, size (both
//     showing the text at the cursor, see Sync) | Text / Background and the
//     colour swatches;
//   * paragraphs: left / centre / right | bulleted and numbered lists, and
//     quote in / out when the host asks for them (Options::quoteTools) |
//     a horizontal line, a link (a web page or an e-mail address) and a
//     picture (inside the line at the cursor), then a stretch spacer the
//     host may put its own controls after (the compose window's
//     Plain text | Formatted).
// Every tool acts on the editor the host names at the moment of the click,
// then gives it the keyboard back.
// Version: 0.4.0 - Options::singleRow; Sync shows the font and size at the
//                  cursor; Text / Background colour switch
// Version: 0.3.0 - icon-only tools in groups on a tinted strip; the quote
//                  tools are in the paragraph row
// Version: 0.2.0 - Quote + / Quote − (Options::quoteTools)
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {
class UltraCanvasContainer;
class UltraCanvasDropdown;
class UltraCanvasRichTextEdit;
struct RichCharFormatState;
class UltraCanvasUIElement;
class UltraCanvasWindowBase;
}

namespace UltraMail {

class FormatBar {
public:
    struct Options {
        std::string idPrefix = "fmt";
        // The editor the tools act on, asked on every click and when a Link…
        // or Picture… dialog answers. Null = gone (the window closed while a
        // dialog was open): the tool does nothing.
        std::function<UltraCanvas::UltraCanvasRichTextEdit*()> editor;
        // The window the Link… and Picture… dialogs belong to.
        UltraCanvas::UltraCanvasWindowBase* dialogParent = nullptr;
        // Quote in / out (after the lists): the paragraphs at the
        // cursor one quote level in or out (a reply's quote bars). For the
        // compose window; a signature has no quotes.
        bool quoteTools = false;
        // Every tool in one row, the paragraph tools after the character
        // ones (the compose window, which is wide enough for it).
        bool singleRow = false;
    };

    // Builds both rows into `root`.
    static FormatBar Build(const Options& options);

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> root;
    // The paragraph row (the only row with Options::singleRow). It ends in a
    // stretch spacer, so anything the host adds lands at its right end.
    UltraCanvas::UltraCanvasContainer* paragraphRow = nullptr;

    // Shows or hides every tool (the host's own controls in the paragraph
    // row stay); hidden, the bar is one row high.
    void SetToolsVisible(bool visible);

    // Font and Size show the text at the cursor, or the selected text: the
    // host calls this whenever the editor's selection or text changes.
    void Sync(const UltraCanvas::RichCharFormatState& state);

private:
    UltraCanvas::UltraCanvasContainer* characterRow_ = nullptr;   // two-row bar only
    std::vector<UltraCanvas::UltraCanvasUIElement*> tools_;
    UltraCanvas::UltraCanvasDropdown* font_ = nullptr;
    UltraCanvas::UltraCanvasDropdown* size_ = nullptr;
    bool singleRow_ = false;
};

} // namespace UltraMail
