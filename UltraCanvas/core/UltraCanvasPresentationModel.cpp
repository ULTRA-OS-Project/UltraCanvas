// core/UltraCanvasPresentationModel.cpp
// What the deck model does beside reading and writing
// (include/UltraCanvasPresentationPptx.h): a new slide from a layout.
//
// Version: 1.1.0 - a shape's text to a rich document and back
// Version: 1.0.1 - a layout's placeholder with no place of its own takes its master's
// Version: 1.0.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework
#include "UltraCanvasPresentationPptx.h"
#include "UltraCanvasPptxSlideRender.h"   // PptxBulletText

#include <cctype>

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

// ===== A SHAPE'S TEXT AND THE RICH DOCUMENT =====

namespace {

std::string ToRichColor(const std::string& hex) { return hex.empty() ? std::string() : "#" + hex; }

std::string FromRichColor(const std::string& color) {
    if (color.empty()) return std::string();
    std::string hex = color[0] == '#' ? color.substr(1) : color;
    for (char& c : hex) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return hex.size() == 6 ? hex : std::string();
}

RichTextRun ToRichRun(const PptxRun& run) {
    RichTextRun r;
    r.text = run.text;
    r.bold = run.bold;
    r.italic = run.italic;
    r.underline = run.underline;
    r.strikethrough = run.strike;
    r.superscript = run.baseline > 0;
    r.subscript = run.baseline < 0;
    r.fontFamily = run.font;
    r.fontSizePt = run.sizePt;
    r.color = ToRichColor(run.color);
    r.highlightColor = ToRichColor(run.highlight);
    r.linkTarget = run.hyperlink;
    return r;
}

PptxRun FromRichRun(const RichTextRun& r, const PptxRun& look) {
    PptxRun run = look;   // what the document does not carry (capitals) stays
    run.text = r.text;
    run.bold = r.bold;
    run.italic = r.italic;
    run.underline = r.underline;
    run.strike = r.strikethrough;
    run.baseline = r.superscript ? 30 : (r.subscript ? -25 : 0);
    run.font = r.fontFamily.empty() ? look.font : r.fontFamily;
    run.sizePt = r.fontSizePt > 0 ? r.fontSizePt : look.sizePt;
    run.color = FromRichColor(r.color);
    run.highlight = FromRichColor(r.highlightColor);
    run.hyperlink = r.linkTarget;
    return run;
}

RichTextAlign ToRichAlign(PptxAlign align) {
    switch (align) {
        case PptxAlign::Center: return RichTextAlign::Center;
        case PptxAlign::Right: return RichTextAlign::Right;
        case PptxAlign::Justify: return RichTextAlign::Justify;
        default: return RichTextAlign::Left;
    }
}

PptxAlign FromRichAlign(RichTextAlign align, PptxAlign fallback) {
    switch (align) {
        case RichTextAlign::Left: return PptxAlign::Left;
        case RichTextAlign::Center: return PptxAlign::Center;
        case RichTextAlign::Right: return PptxAlign::Right;
        case RichTextAlign::Justify: return PptxAlign::Justify;
        default: return fallback;
    }
}

// PowerPoint's numbering schemes ("alphaLcParenR") as the rich model's
// number format and label template ("%1)"), and back.
void ToRichNumbering(const std::string& scheme, int level, RichNumberFormat& format, std::string& numberTemplate) {
    const std::string n = "%" + std::to_string(level + 1);
    format = scheme.rfind("alphaLc", 0) == 0 ? RichNumberFormat::LowerLetter
           : scheme.rfind("alphaUc", 0) == 0 ? RichNumberFormat::UpperLetter
           : scheme.rfind("romanLc", 0) == 0 ? RichNumberFormat::LowerRoman
           : scheme.rfind("romanUc", 0) == 0 ? RichNumberFormat::UpperRoman
                                             : RichNumberFormat::Decimal;
    auto ends = [&scheme](const char* tail) {
        const std::string t = tail;
        return scheme.size() >= t.size() && scheme.compare(scheme.size() - t.size(), t.size(), t) == 0;
    };
    numberTemplate = ends("ParenBoth") ? "(" + n + ")" : ends("ParenR") ? n + ")" : ends("Plain") ? n : n + ".";
}

std::string FromRichNumbering(RichNumberFormat format, const std::string& numberTemplate) {
    std::string kind;
    switch (format) {
        case RichNumberFormat::LowerLetter: kind = "alphaLc"; break;
        case RichNumberFormat::UpperLetter: kind = "alphaUc"; break;
        case RichNumberFormat::LowerRoman: kind = "romanLc"; break;
        case RichNumberFormat::UpperRoman: kind = "romanUc"; break;
        default: kind = "arabic"; break;
    }
    const bool open = !numberTemplate.empty() && numberTemplate.front() == '(';
    const char last = numberTemplate.empty() ? '.' : numberTemplate.back();
    if (open) return kind + "ParenBoth";
    if (last == ')') return kind + "ParenR";
    if (last == '.') return kind + "Period";
    return kind == "arabic" ? "arabicPlain" : kind + "Period";
}

} // namespace

UCRichDocument PptxParagraphsToRichDocument(const std::vector<PptxParagraph>& paragraphs) {
    UCRichDocument document;
    for (const PptxParagraph& paragraph : paragraphs) {
        RichDocBlock block;
        block.type = paragraph.bullet ? RichBlockType::ListItem : RichBlockType::Paragraph;
        block.orderedList = paragraph.numbered;
        block.listLevel = paragraph.level;
        if (paragraph.numbered) {
            ToRichNumbering(paragraph.numberScheme, paragraph.level, block.numberFormat, block.numberTemplate);
            // The start number on a list's first item only: the next ones count on.
            const size_t index = static_cast<size_t>(&paragraph - paragraphs.data());
            const bool first = index == 0 || !paragraphs[index - 1].numbered || paragraphs[index - 1].level != paragraph.level;
            if (first && paragraph.numberStartAt > 1) block.listStartNumber = paragraph.numberStartAt;
        } else if (paragraph.bullet) {
            block.bulletText = PptxBulletText(paragraph);   // a Wingdings square as the square it is
        }
        block.align = ToRichAlign(paragraph.align);
        bool breakNext = false;
        for (const PptxRun& run : paragraph.runs) {
            if (run.text == "\n") { breakNext = true; continue; }
            RichTextRun r = ToRichRun(run);
            r.lineBreakBefore = breakNext;
            breakNext = false;
            block.runs.push_back(std::move(r));
        }
        document.blocks.push_back(std::move(block));
    }
    if (document.blocks.empty()) document.blocks.emplace_back();
    return document;
}

std::vector<PptxParagraph> PptxParagraphsFromRichDocument(const UCRichDocument& document,
                                                          const std::vector<PptxParagraph>& previous) {
    std::vector<PptxParagraph> out;
    PptxRun lastLook = previous.empty() || previous.front().runs.empty() ? PptxRun() : previous.front().runs.front();
    lastLook.text.clear();
    lastLook.hyperlink.clear();
    for (size_t i = 0; i < document.blocks.size(); ++i) {
        const RichDocBlock& block = document.blocks[i];
        const bool listItem = block.type == RichBlockType::ListItem;
        const int level = listItem ? block.listLevel : 0;
        // The paragraph it was, or one like it.
        const PptxParagraph* model = i < previous.size() ? &previous[i] : nullptr;
        if (!model || model->level != level) {
            for (auto it = previous.rbegin(); it != previous.rend(); ++it)
                if (it->level == level) { model = &*it; break; }
        }
        if (!model && !previous.empty()) model = &previous.front();
        PptxParagraph paragraph = model ? *model : PptxParagraph();
        paragraph.runs.clear();
        paragraph.level = level;
        paragraph.bullet = listItem;
        paragraph.numbered = listItem && block.orderedList;
        if (paragraph.numbered) {
            // The scheme as the list now reads, unless it reads as it did.
            RichNumberFormat was = RichNumberFormat::Decimal;
            std::string wasTemplate;
            ToRichNumbering(paragraph.numberScheme, level, was, wasTemplate);
            if (paragraph.numberScheme.empty() || was != block.numberFormat ||
                (!block.numberTemplate.empty() && wasTemplate != block.numberTemplate))
                paragraph.numberScheme = FromRichNumbering(block.numberFormat, block.numberTemplate);
            if (block.listStartNumber > 0) paragraph.numberStartAt = block.listStartNumber;
        } else if (listItem && !block.bulletText.empty() && (!model || !model->bullet || block.bulletText != PptxBulletText(*model))) {
            // A bullet chosen in the editor: its character, in no symbol typeface.
            paragraph.bulletChar = block.bulletText;
            paragraph.bulletFont.clear();
        }
        if (paragraph.bullet && !paragraph.numbered && paragraph.bulletChar.empty() && (!model || !model->bullet))
            paragraph.bulletChar = "\xE2\x80\xA2";   // a dot, where the paragraph had none
        paragraph.align = FromRichAlign(block.align, paragraph.align);
        PptxRun look = model && !model->runs.empty() ? model->runs.front() : lastLook;
        look.text.clear();
        look.hyperlink.clear();
        for (const RichTextRun& r : block.runs) {
            if (r.lineBreakBefore && !paragraph.runs.empty()) {
                PptxRun lineBreak = paragraph.runs.back();
                lineBreak.text = "\n";
                paragraph.runs.push_back(lineBreak);
            }
            if (r.text.empty() && !block.runs.empty() && block.runs.size() > 1) continue;
            PptxRun run = FromRichRun(r, look);
            look = run;
            look.text.clear();
            look.hyperlink.clear();
            paragraph.runs.push_back(std::move(run));
        }
        if (paragraph.runs.empty()) paragraph.runs.push_back(look);   // an empty paragraph keeps its look
        lastLook = paragraph.runs.back();
        lastLook.text.clear();
        lastLook.hyperlink.clear();
        out.push_back(std::move(paragraph));
    }
    return out;
}

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
            PptxShape shape = Emptied(placeholder);
            // A placeholder the layout gives no place takes its master's of the same kind.
            if ((shape.width <= 0 || shape.height <= 0) && layout.masterIndex >= 0 &&
                layout.masterIndex < static_cast<int>(deck.masters.size())) {
                for (const PptxShape& m : deck.masters[static_cast<size_t>(layout.masterIndex)].placeholders) {
                    if (m.kind != shape.kind || m.width <= 0 || m.height <= 0) continue;
                    shape.x = m.x;
                    shape.y = m.y;
                    shape.width = m.width;
                    shape.height = m.height;
                    break;
                }
            }
            slide.shapes.push_back(std::move(shape));
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
