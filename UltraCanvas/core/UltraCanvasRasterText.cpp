// core/UltraCanvasRasterText.cpp
// Draws a text layer's words into its pixels through UltraCanvasRichTextEdit,
// the element that edits them on the canvas, so the two agree to the pixel.
// Version: 1.0.1 - the canvas editor and the renderer show no font notice
// Version: 1.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasRasterText.h"
#include "UltraCanvasRichDocument.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasUtilsUtf8.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace UltraCanvas {

namespace {
    // Text sizes go to the text engine in points at 96 dpi; a bitmap editor
    // thinks in pixels.
    constexpr float kPointsPerPixel = 72.0f / 96.0f;

    // Taller than any text a layer can hold: the first pass lays the text out
    // in an element this tall, so nothing scrolls while it is measured.
    constexpr float kMeasureHeight = 1.0e6f;

    // The widest box the renderer accepts: a pixmap side, and far past any
    // canvas a bitmap editor opens.
    constexpr int kMaxBoxSide = 32767;

    bool IsBlank(const std::string& s) {
        return std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isspace(c) != 0; });
    }

    // The element puts its own block spacing between two paragraphs that
    // state none, and the stated spacing otherwise. A file format writes
    // "none" as its own default (ODT: 6 pt below), so a text read back would
    // be spaced differently. Stating the spacing the element used - the same
    // gaps, by its rule - makes the words read back from any file exactly
    // as they were laid out.
    void StateParagraphSpacing(UCRichDocument& document, float blockSpacingPx) {
        auto& blocks = document.blocks;
        for (size_t i = 0; i < blocks.size(); ++i) {
            RichDocBlock& block = blocks[i];
            if (i + 1 < blocks.size() && block.spaceAfterPt < 0.0f && blocks[i + 1].spaceBeforePt < 0.0f) {
                block.spaceAfterPt = blockSpacingPx * kPointsPerPixel;
            }
            if (block.spaceAfterPt < 0.0f) block.spaceAfterPt = 0.0f;
            if (block.spaceBeforePt < 0.0f) block.spaceBeforePt = 0.0f;
        }
    }
}

RichTextEditStyle TextLayerEditStyle(const RasterTextContent& content) {
    RichTextEditStyle style;
    style.baseFont.fontFamily = content.fontFamily.empty() ? std::string("Sans") : content.fontFamily;
    style.baseFont.fontSize = std::max(1.0f, content.fontSizePx) * kPointsPerPixel;
    style.baseFont.fontWeight = FontWeight::Normal;
    style.baseFont.fontSlant = FontSlant::Normal;
    style.textColor = content.colour.ToColor();
    style.backgroundColor = Colors::Transparent;
    style.drawBorder = false;
    style.padding = 0.0f;
    // What the editor shows on top of the words while they are edited; the
    // renderer draws neither.
    style.selectionColor = Color(64, 128, 255, 96);
    style.cursorColor = Color(content.colour.r, content.colour.g, content.colour.b, 255);
    return style;
}

void PrepareTextLayerEditor(UltraCanvasRichTextEdit& editor, const RasterTextContent& content, double zoom) {
    editor.SetStyle(TextLayerEditStyle(content));
    editor.SetSpellCheckEnabled(false);
    editor.SetShowComments(false);
    // On the canvas it draws exactly the layer's pixels: no notice over them.
    editor.SetShowFontNotice(false);
    editor.SetZoomRange(1.0f / 64.0f, 64.0f);
    editor.SetZoom(static_cast<float>(zoom));
}

std::shared_ptr<UCRasterLayer> RenderTextLayer(const RasterTextContent& content,
                                               int canvasWidth, int canvasHeight,
                                               std::string& error) {
    if (!content.document) {
        error = "The text layer has no text";
        return nullptr;
    }
    if (canvasWidth <= 0 || canvasHeight <= 0) {
        error = "The canvas has no size";
        return nullptr;
    }
    const int boxWidth = static_cast<int>(std::ceil(content.width));
    if (boxWidth <= 0 || boxWidth > kMaxBoxSide) {
        error = "The text box has no usable width";
        return nullptr;
    }

    auto result = std::make_shared<RasterTextContent>(content);
    result->x = std::round(content.x);
    result->y = std::round(content.y);
    const RichTextEditStyle style = TextLayerEditStyle(content);
    // The layer keeps its own copy, spacing stated: the content's document
    // may be shared with undo history and must not change.
    auto stated = std::make_shared<UCRichDocument>(*content.document);
    StateParagraphSpacing(*stated, style.blockSpacing);
    result->document = stated;

    // The element edits what it is given, so it gets a copy of that copy.
    // Shared, as elements are: some of what it does asks for itself that way.
    auto element = std::make_shared<UltraCanvasRichTextEdit>("RasterTextRender", 0, 0,
                                                             static_cast<float>(boxWidth), kMeasureHeight);
    element->SetStyle(style);
    element->SetSpellCheckEnabled(false);
    element->SetShowComments(false);
    element->SetShowFontNotice(false);
    element->SetReadOnly(true);
    element->SetDocument(std::make_shared<UCRichDocument>(*stated));

    // Lay out first, for the height the text takes ...
    std::unique_ptr<IRenderContext> measure = CreateRenderContext(Size2Di(boxWidth, 1), nullptr);
    if (!measure) {
        error = "Could not create an offscreen render context";
        return nullptr;
    }
    element->Render(measure.get(), Rect2Df(0, 0, static_cast<float>(boxWidth), kMeasureHeight));
    result->height = std::max(0.0f, element->GetContentHeight());
    const int boxHeight = std::max(1, static_cast<int>(std::ceil(result->height)));
    if (boxHeight > kMaxBoxSide) {
        error = "The text is too long for one text layer";
        return nullptr;
    }

    // ... then draw it in an element exactly that tall (so it never scrolls),
    // laid out again with the context it is drawn into.
    UCPixmap pixmap;
    if (!pixmap.Init(boxWidth, boxHeight)) {
        error = "Could not allocate the text's pixels";
        return nullptr;
    }
    std::unique_ptr<IRenderContext> ctx = CreateRenderContext(Size2Di(boxWidth, boxHeight), nullptr);
    if (!ctx) {
        error = "Could not create an offscreen render context";
        return nullptr;
    }
    element->SetBounds(Rect2Df(0, 0, static_cast<float>(boxWidth), static_cast<float>(boxHeight)));
    element->InvalidateDocument();
    ctx->Clear(Colors::Transparent);
    element->Render(ctx.get(), Rect2Df(0, 0, static_cast<float>(boxWidth), static_cast<float>(boxHeight)));
    ctx->FlushToSurface(pixmap.GetSurface(), Point2Dd(0, 0));
    pixmap.MarkDirty();
    pixmap.Flush();
    const uint32_t* src = pixmap.GetPixelData();
    if (!src) {
        error = "The offscreen surface exposed no pixels";
        return nullptr;
    }

    auto layer = std::make_shared<UCRasterLayer>(canvasWidth, canvasHeight, TextLayerName(*content.document));
    if (!layer->IsValid()) {
        error = "Could not allocate the text layer";
        return nullptr;
    }
    // Premultiplied ARGB32 (the surface's format) into the layer's straight
    // RGBA, clipped to the canvas.
    const int stride = pixmap.GetRawWidth();
    const int ox = static_cast<int>(result->x), oy = static_cast<int>(result->y);
    for (int y = std::max(0, -oy); y < boxHeight && oy + y < canvasHeight; ++y) {
        const uint32_t* row = src + static_cast<size_t>(y) * stride;
        uint8_t* dst = layer->Row(oy + y);
        for (int x = std::max(0, -ox); x < boxWidth && ox + x < canvasWidth; ++x) {
            const uint32_t p = row[x];
            const int a = static_cast<int>((p >> 24) & 0xFF);
            if (a == 0) continue;
            int r = static_cast<int>((p >> 16) & 0xFF);
            int g = static_cast<int>((p >> 8) & 0xFF);
            int b = static_cast<int>(p & 0xFF);
            if (a != 255) {
                r = std::min(255, (r * 255 + a / 2) / a);
                g = std::min(255, (g * 255 + a / 2) / a);
                b = std::min(255, (b * 255 + a / 2) / a);
            }
            uint8_t* d = dst + static_cast<size_t>(ox + x) * 4;
            d[0] = static_cast<uint8_t>(r);
            d[1] = static_cast<uint8_t>(g);
            d[2] = static_cast<uint8_t>(b);
            d[3] = static_cast<uint8_t>(a);
        }
    }
    layer->text = result;
    return layer;
}

std::shared_ptr<UCRasterLayer> RenderTextLayerLike(const UCRasterLayer& layer,
                                                   const RasterTextContent& content,
                                                   std::string& error) {
    auto rendered = RenderTextLayer(content, layer.GetWidth(), layer.GetHeight(), error);
    if (!rendered) return nullptr;
    rendered->name = layer.name;
    rendered->visible = layer.visible;
    rendered->locked = layer.locked;
    rendered->opacity = layer.opacity;
    rendered->blendMode = layer.blendMode;
    return rendered;
}

std::string TextLayerName(const UCRichDocument& document, size_t maxCharacters) {
    const std::string plain = document.ToPlainText();
    size_t start = 0;
    while (start < plain.size()) {
        size_t end = plain.find('\n', start);
        if (end == std::string::npos) end = plain.size();
        std::string line = plain.substr(start, end - start);
        const size_t first = line.find_first_not_of(" \t\r");
        if (first != std::string::npos) {
            line = line.substr(first, line.find_last_not_of(" \t\r") - first + 1);
            const int limit = static_cast<int>(std::max<size_t>(1, maxCharacters));
            if (utf8_length(line) > limit) {
                line = line.substr(0, utf8_bytes_for_chars(line, limit - 1)) + "\xE2\x80\xA6";   // …
            }
            return line;
        }
        start = end + 1;
    }
    return "Text";
}

bool IsTextLayerDocumentEmpty(const UCRichDocument& document) {
    return document.media.empty() && IsBlank(document.ToPlainText());
}

} // namespace UltraCanvas
