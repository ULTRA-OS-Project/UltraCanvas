// Apps/UltraMail/ui/UltraMailFormatBar.h
// The formatting toolbar for an UltraCanvasRichTextEdit, shared by the
// compose window and the signature editor. Two rows:
//   * characters: bold, italic, underline, strikethrough, font, size, colour,
//     and Quote + / Quote − when the host asks for them (Options::quoteTools);
//   * paragraphs: left / centre / right, bulleted and numbered lists, a
//     horizontal line, Link… (a web page or an e-mail address) and Picture…
//     (inside the line at the cursor), then a stretch spacer the host may put
//     its own controls after (the compose window's Plain text | Formatted).
// Every tool acts on the editor the host names at the moment of the click,
// then gives it the keyboard back.
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
class UltraCanvasRichTextEdit;
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
        // Quote + / Quote − (end of the character row): the paragraphs at the
        // cursor one quote level in or out (a reply's quote bars). For the
        // compose window; a signature has no quotes.
        bool quoteTools = false;
    };

    // Builds both rows into `root`.
    static FormatBar Build(const Options& options);

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> root;
    // The paragraph row. It ends in a stretch spacer, so anything the host
    // adds lands at its right end.
    UltraCanvas::UltraCanvasContainer* paragraphRow = nullptr;

    // Shows or hides every tool (the host's own controls in the paragraph
    // row stay); hidden, the bar is one row high.
    void SetToolsVisible(bool visible);

private:
    UltraCanvas::UltraCanvasContainer* characterRow_ = nullptr;
    std::vector<UltraCanvas::UltraCanvasUIElement*> paragraphTools_;
};

} // namespace UltraMail
