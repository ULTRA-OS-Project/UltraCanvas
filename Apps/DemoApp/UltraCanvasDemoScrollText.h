// Apps/DemoApp/UltraCanvasDemoScrollText.h
// A read-only text block that grows a vertical scrollbar when its text does
// not fit the box it was given. Header-only; used by the 3D Graphics demo
// pages, whose information panels are fixed rectangles filled with text of a
// length nobody can predict at build time.
//
// "What the reader found" says a great deal more about a COLLADA scene than
// about a STEP solid, and "What the format can carry" grows a paragraph when
// the build has no converter for the extension. A label alone cannot cope
// with that: it is centred in its box by default, so text that overruns the
// box loses its FIRST line as well as its last, and there is no way for the
// visitor to see what was cut.
//
// So the text goes in an UltraCanvasContainer sized to the space available,
// with an auto-sized UltraCanvasLabel as its only child: the layout engine
// measures the label against the real text, the container notices its child
// is taller than the viewport, and the scrollbar appears by itself. When the
// text does fit, nothing is drawn that was not drawn before.
//
// Version: 1.0.0
// Last Modified: 2026-09-17
// Author: UltraCanvas Framework

#pragma once

#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"

#include <memory>
#include <string>

namespace UltraCanvas {
namespace demoui {

// A scrolling text block: the viewport, and the label that holds the text.
// Both are exposed because callers still place the viewport and occasionally
// restyle the label; SetText() is the part worth having a method for.
struct ScrollingText {
    std::shared_ptr<UltraCanvasContainer> View;
    std::shared_ptr<UltraCanvasLabel> Text;

    // Replaces the text and returns to the top - a panel describing a new
    // sample must start at its first line, not wherever the previous sample
    // was left scrolled to.
    void SetText(const std::string& newText) const {
        if (!Text) return;
        Text->SetText(newText);
        if (View) View->ScrollToVertical(0);
    }
};

// Builds a block at (x, y) of exactly (w, h) - the same rectangle a plain
// label would have occupied. fontSize and textColor match what the demo
// pages set on their information labels.
inline ScrollingText MakeScrollingText(const std::string& id,
                                       float x, float y, float w, float h,
                                       float fontSize = 11.0f,
                                       const Color& textColor = Color(50, 50, 50, 255)) {
    ScrollingText block;

    block.View = std::make_shared<UltraCanvasContainer>(id + "Scroll", x, y, w, h);
    ContainerStyle style = block.View->GetContainerStyle();
    style.autoShowScrollbars = true;
    style.autoShowVerticalScrollbar = true;
    // The lines are preformatted (columns aligned with spaces), so they are
    // laid out to the viewport width and never report horizontal overflow;
    // a horizontal bar would only ever be an empty track.
    style.autoShowHorizontalScrollbar = false;
    block.View->SetContainerStyle(style);

    // The bar is drawn in the right-hand padding, so reserving its width here
    // keeps it off the text instead of on top of the last few characters.
    block.View->SetPadding(0.0f, static_cast<float>(style.scrollbarStyle.trackSize), 0.0f, 0.0f);

    // No explicit size: the engine gives the label the viewport's width and
    // measures its height from the text, which is what makes the container
    // scrollable. Top alignment because a scrolled block starts at its first
    // line - the label default (Middle) centres short text in a tall box and,
    // worse, hides the top of text that overflows.
    block.Text = std::make_shared<UltraCanvasLabel>(id, 0.0f, 0.0f, 0.0f, 0.0f);
    block.Text->SetFontSize(fontSize);
    block.Text->SetTextColor(textColor);
    block.Text->SetAlignment(TextAlignment::Left, VerticalAlignment::Top);
    // Wrapped, because the alternative here is not a wider box: a line longer
    // than the panel (a format name with its version, a generator string) is
    // ellipsized away otherwise, and the part that got cut is exactly the part
    // worth reading. The aligned columns these panels are built from are far
    // shorter than the box and are unaffected.
    block.Text->SetWrap(TextWrap::WrapWord);
    block.View->AddChild(block.Text);

    return block;
}

} // namespace demoui
} // namespace UltraCanvas
