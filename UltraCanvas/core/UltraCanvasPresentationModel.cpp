// core/UltraCanvasPresentationModel.cpp
// What the deck model does beside reading and writing
// (include/UltraCanvasPresentationPptx.h): a new slide from a layout.
//
// Version: 1.0.0
// Author: UltraCanvas Framework
#include "UltraCanvasPresentationPptx.h"

namespace UltraCanvas {

namespace {

constexpr int64_t kDefaultSlideWidth = 12192000;    // 16:9, EMU
constexpr int64_t kDefaultSlideHeight = 6858000;

// A placeholder emptied for a new slide: one paragraph with one empty run,
// keeping the first ones' look, so what is typed into it looks as the
// layout says.
PptxShape Emptied(const PptxShape& placeholder) {
    PptxShape shape = placeholder;
    PptxParagraph paragraph;
    if (!placeholder.paragraphs.empty()) {
        paragraph = placeholder.paragraphs.front();
        PptxRun run = paragraph.runs.empty() ? PptxRun() : paragraph.runs.front();
        run.text.clear();
        run.hyperlink.clear();
        paragraph.runs = {run};
        paragraph.level = 0;
    } else {
        paragraph.runs.push_back(PptxRun());
    }
    shape.paragraphs = {paragraph};
    shape.placeInherited = false;
    return shape;
}

PptxShape Placeholder(PptxShapeKind kind, const char* type, const char* name, int64_t x, int64_t y, int64_t w, int64_t h,
                      float sizePt) {
    PptxShape shape;
    shape.kind = kind;
    shape.placeholder = type;
    shape.name = name;
    shape.x = x; shape.y = y; shape.width = w; shape.height = h;
    shape.anchor = kind == PptxShapeKind::Title ? PptxAnchor::Middle : PptxAnchor::Top;
    PptxParagraph paragraph;
    paragraph.bullet = kind == PptxShapeKind::Body;
    PptxRun run;
    run.sizePt = sizePt;
    paragraph.runs.push_back(run);
    shape.paragraphs.push_back(paragraph);
    return shape;
}

} // namespace

PptxSlide PptxSlideFromLayout(const PptxPresentation& deck, int layoutIndex) {
    PptxSlide slide;
    if (layoutIndex >= 0 && layoutIndex < static_cast<int>(deck.layouts.size())) {
        const PptxLayout& layout = deck.layouts[static_cast<size_t>(layoutIndex)];
        slide.layoutIndex = layoutIndex;
        slide.background = layout.background;
        if (slide.background.empty() && layout.masterIndex >= 0 && layout.masterIndex < static_cast<int>(deck.masters.size()))
            slide.background = deck.masters[static_cast<size_t>(layout.masterIndex)].background;
        for (const PptxShape& placeholder : layout.placeholders) {
            // Date, footer and slide number belong to the master's furniture,
            // not to a new slide's content.
            if (placeholder.placeholder == "dt" || placeholder.placeholder == "ftr" || placeholder.placeholder == "sldNum")
                continue;
            slide.shapes.push_back(Emptied(placeholder));
        }
        return slide;
    }
    // No layouts known: PowerPoint's Title and Content, on the deck's slide.
    const int64_t w = deck.slideWidth > 0 ? deck.slideWidth : kDefaultSlideWidth;
    const int64_t h = deck.slideHeight > 0 ? deck.slideHeight : kDefaultSlideHeight;
    const int64_t margin = w / 16;
    slide.shapes.push_back(Placeholder(PptxShapeKind::Title, "title", "Title 1", margin, h / 20, w - 2 * margin, h * 4 / 21, 44));
    slide.shapes.push_back(Placeholder(PptxShapeKind::Body, "body", "Content Placeholder 2", margin, h * 13 / 48,
                                       w - 2 * margin, h * 31 / 48, 28));
    return slide;
}

} // namespace UltraCanvas
