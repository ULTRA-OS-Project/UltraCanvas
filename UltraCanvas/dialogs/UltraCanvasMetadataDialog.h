// dialogs/UltraCanvasMetadataDialog.h
// A read-only popup for the metadata a file carries, shown in an
// UltraCanvasTextArea: Markdown (headings and a laid-out table) or plain
// text, whichever the caller asks for.
//
// It takes text, not fields, so anything that can describe itself can use it:
// PixelFX::Header::MetadataToText() produces it for an image,
// and a document, font or audio reader can produce the same shape.
// Version: 1.0.0
// Last Modified: 2026-09-13
// Author: UltraCanvas Framework
#pragma once

#include "../include/UltraCanvasWindow.h"
#include "../include/UltraCanvasContainer.h"
#include "../include/UltraCanvasTextArea.h"
#include "../include/UltraCanvasButton.h"

#include <memory>
#include <string>

namespace UltraCanvas {

    class UltraCanvasMetadataDialog : public UltraCanvasWindow {
    public:
        // `subject` heads the window (usually the file name). `markdown` true
        // renders the text as Markdown — headings and tables — false shows it
        // as it is.
        UltraCanvasMetadataDialog(const std::string& subject, std::string text, bool markdown = true);

        const std::string& GetText() const { return text; }

    private:
        std::string text;
        std::shared_ptr<UltraCanvasTextArea> view;
        std::shared_ptr<UltraCanvasButton> copyButton;
        std::shared_ptr<UltraCanvasButton> closeButton;
    };

    // Creates, shows and returns the popup, parented to `parent` when given so
    // the window manager keeps it above the window that opened it.
    std::shared_ptr<UltraCanvasMetadataDialog> ShowMetadataDialog(
            const std::string& subject, const std::string& text, bool markdown = true,
            UltraCanvasWindowBase* parent = nullptr);

} // namespace UltraCanvas
