// core/UltraCanvasPptxSlideRender.cpp
// A PowerPoint slide drawn as a picture - see UltraCanvasPptxSlideRender.h.
// Version: 1.0.0
// Last Modified: 2026-11-18
// Author: UltraCanvas Framework

#include "UltraCanvasPptxSlideRender.h"
#include "UltraCanvasRenderContext.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

namespace UltraCanvas {

namespace {

constexpr double kEmuPerPoint = 12700.0;
constexpr int64_t kDefaultSlideWidth = 12192000;   // 16:9
constexpr int64_t kDefaultSlideHeight = 6858000;
constexpr int64_t kInsetX = 91440;                 // a text box's default insets: 0.1", 0.05"
constexpr int64_t kInsetY = 45720;
constexpr int64_t kLevelIndent = 457200;           // 0.5" per outline level

// As UltraCanvasFilePreviewsBuiltin: pictures decode through libvips, which a
// headless caller may not have initialised.
bool EnsureImageSubsystem() {
#ifdef HAS_LIBVIPS
    static const bool ok = UCImageRaster::InitializeImageSubsysterm(nullptr);
    return ok;
#else
    return true;
#endif
}

Color FromHex(const std::string& hex, const Color& fallback) {
    if (hex.size() != 6) return fallback;
    const unsigned long rgb = std::strtoul(hex.c_str(), nullptr, 16);
    return Color(static_cast<uint8_t>((rgb >> 16) & 0xFF), static_cast<uint8_t>((rgb >> 8) & 0xFF),
                 static_cast<uint8_t>(rgb & 0xFF));
}

Color Mix(const Color& a, const Color& b, double t) {
    auto channel = [t](uint8_t x, uint8_t y) {
        return static_cast<uint8_t>(std::lround(x + (y - x) * t));
    };
    return Color(channel(a.r, b.r), channel(a.g, b.g), channel(a.b, b.b));
}

TextAlignment Alignment(PptxAlign align) {
    switch (align) {
        case PptxAlign::Center: return TextAlignment::Center;
        case PptxAlign::Right: return TextAlignment::Right;
        case PptxAlign::Justify: return TextAlignment::Justify;
        default: return TextAlignment::Left;
    }
}

// The size a run takes when neither it nor any style says: PowerPoint's own.
float DefaultSize(PptxShapeKind kind, int level) {
    switch (kind) {
        case PptxShapeKind::Title: return 44;
        case PptxShapeKind::Subtitle: return 24;
        case PptxShapeKind::Body: return std::max(16.0f, 28.0f - 4.0f * static_cast<float>(level));
        default: return 18;
    }
}

// Where a placeholder that has no place anywhere goes, in parts of the slide.
Rect2Dd FallbackBox(PptxShapeKind kind) {
    switch (kind) {
        case PptxShapeKind::Title: return Rect2Dd(0.07, 0.05, 0.86, 0.18);
        case PptxShapeKind::Subtitle: return Rect2Dd(0.15, 0.55, 0.7, 0.2);
        default: return Rect2Dd(0.07, 0.26, 0.86, 0.66);
    }
}

class SlidePainter {
public:
    SlidePainter(IRenderContext* ctx, const PptxPresentation& deck, const PptxSlide& slide, double scale)
        : ctx_(ctx), deck_(deck), slide_(slide), scale_(scale),
          ink_(FromHex(slide.textColor, Colors::Black)) {}

    void Paint(double width, double height) {
        width_ = width;
        height_ = height;
        ctx_->Clear(FromHex(slide_.background, Colors::White));
        for (const PptxShape& shape : slide_.shapes) {
            const Rect2Dd box = BoxOf(shape);
            if (box.width <= 1 || box.height <= 1) continue;
            switch (shape.kind) {
                case PptxShapeKind::Picture: PaintPicture(shape, box); break;
                case PptxShapeKind::Table: PaintTable(shape, box); break;
                default:
                    PaintGeometry(shape, box);
                    PaintText(shape, box);
                    break;
            }
        }
    }

private:
    IRenderContext* ctx_;
    const PptxPresentation& deck_;
    const PptxSlide& slide_;
    double scale_;   // pixels per EMU
    Color ink_;
    double width_ = 0, height_ = 0;

    double Px(int64_t emu) const { return static_cast<double>(emu) * scale_; }
    double PxOfPoints(double points) const { return points * kEmuPerPoint * scale_; }

    Rect2Dd BoxOf(const PptxShape& shape) const {
        if (shape.width > 0 && shape.height > 0)
            return Rect2Dd(Px(shape.x), Px(shape.y), Px(shape.width), Px(shape.height));
        if (shape.kind == PptxShapeKind::Text || shape.kind == PptxShapeKind::Picture ||
            shape.kind == PptxShapeKind::Table)
            return Rect2Dd(0, 0, 0, 0);
        const Rect2Dd part = FallbackBox(shape.kind);
        return Rect2Dd(part.x * width_, part.y * height_, part.width * width_, part.height * height_);
    }

    void PaintGeometry(const PptxShape& shape, const Rect2Dd& box) {
        if (shape.fill.empty() && shape.line.empty()) return;
        const double lineWidth = shape.line.empty() ? 0.0 : std::max(1.0, PxOfPoints(shape.lineWidthPt));
        if (shape.geometry == "ellipse") {
            if (!shape.fill.empty()) {
                ctx_->SetFillPaint(FromHex(shape.fill, Colors::Transparent));
                ctx_->FillEllipse(box);
            }
            if (lineWidth > 0) {
                ctx_->SetStrokePaint(FromHex(shape.line, Colors::Transparent));
                ctx_->SetStrokeWidth(lineWidth);
                ctx_->DrawEllipse(box);
            }
            return;
        }
        const float radius = shape.geometry == "roundRect"
                                 ? static_cast<float>(std::min(box.width, box.height) * 0.1667) : 0.0f;
        ctx_->DrawFilledRectangle(box, FromHex(shape.fill, Colors::Transparent), static_cast<float>(lineWidth),
                                  FromHex(shape.line, Colors::Transparent), radius);
    }

    void PaintPicture(const PptxShape& shape, const Rect2Dd& box) {
        const auto bytes = deck_.pictures.find(shape.pictureTarget);
        if (bytes != deck_.pictures.end() && !bytes->second.empty()) {
            if (auto image = UCImage::LoadFromMemory(bytes->second)) {
                ctx_->DrawImage(*image, box, ImageFitMode::Fill);
                return;
            }
        }
        // Not read or not decodable: where it stands, and what it shows.
        ctx_->DrawFilledRectangle(box, Color(236, 238, 241), 1.0f, Color(200, 204, 210));
        if (!shape.altText.empty()) {
            PptxParagraph p;
            p.align = PptxAlign::Center;
            p.runs.push_back({shape.altText, false, true, false, 14, "6B7280"});
            PaintParagraphs({p}, PptxShapeKind::Text, Rect2Dd(box.x + 6, box.y, box.width - 12, box.height),
                            PptxAnchor::Middle, 1.0f, false);
        }
    }

    void PaintTable(const PptxShape& shape, const Rect2Dd& box) {
        if (shape.rows.empty()) return;
        size_t columns = shape.columnWidths.size();
        for (const auto& row : shape.rows) columns = std::max(columns, row.size());
        if (columns == 0) return;
        std::vector<double> widths(columns, box.width / static_cast<double>(columns));
        if (shape.columnWidths.size() == columns) {
            for (size_t c = 0; c < columns; ++c) widths[c] = Px(shape.columnWidths[c]);
        }
        const Color accent = FromHex(ValueOr(deck_.themeColors, "accent1", "4472C4"), Color(68, 114, 196));
        const Color band1 = Mix(accent, Colors::White, 0.8), band2 = Mix(accent, Colors::White, 0.9);
        double y = box.y;
        for (size_t r = 0; r < shape.rows.size(); ++r) {
            const bool header = r == 0;
            // As tall as the row asks, or as its text needs.
            double rowHeight = r < shape.rowHeights.size() ? Px(shape.rowHeights[r]) : 0.0;
            double x = box.x;
            for (size_t c = 0; c < shape.rows[r].size() && c < columns; ++c) {
                const double need = MeasureParagraphs(shape.rows[r][c], widths[c] - 2 * Px(kInsetX));
                rowHeight = std::max(rowHeight, need + 2 * Px(kInsetY));
            }
            for (size_t c = 0; c < columns; ++c) {
                const Rect2Dd cell(x, y, widths[c], rowHeight);
                ctx_->DrawFilledRectangle(cell, header ? accent : (r % 2 ? band1 : band2), 1.0f, Colors::White);
                if (c < shape.rows[r].size()) {
                    std::vector<PptxParagraph> text = shape.rows[r][c];
                    if (header) {
                        for (PptxParagraph& p : text)
                            for (PptxRun& run : p.runs) {
                                run.bold = true;
                                if (run.color.empty()) run.color = "FFFFFF";
                            }
                    }
                    PaintParagraphs(text, PptxShapeKind::Text, Deflate(cell), PptxAnchor::Middle, 1.0f, false);
                }
                x += widths[c];
            }
            y += rowHeight;
        }
    }

    static std::string ValueOr(const std::map<std::string, std::string>& map, const std::string& key,
                               const std::string& fallback) {
        const auto found = map.find(key);
        return found == map.end() || found->second.empty() ? fallback : found->second;
    }

    Rect2Dd Deflate(const Rect2Dd& box) const {
        const double ix = Px(kInsetX), iy = Px(kInsetY);
        return Rect2Dd(box.x + ix, box.y + iy, std::max(1.0, box.width - 2 * ix), std::max(1.0, box.height - 2 * iy));
    }

    void PaintText(const PptxShape& shape, const Rect2Dd& box) {
        bool any = false;
        for (const PptxParagraph& p : shape.paragraphs) any = any || !p.Text().empty();
        if (!any) return;
        // Placeholders shrink to fit, as PowerPoint's autofit does.
        const bool shrink = !shape.placeholder.empty() || shape.fontScale < 1.0f;
        PaintParagraphs(shape.paragraphs, shape.kind, Deflate(box), shape.anchor, shape.fontScale, shrink);
    }

    // ----- Text -----

    struct LaidOut {
        std::unique_ptr<ITextLayout> layout;
        double x = 0, top = 0, height = 0;
    };

    // The paragraphs set into `box` at `scale` of their sizes; their total height.
    std::vector<LaidOut> Lay(const std::vector<PptxParagraph>& paragraphs, PptxShapeKind kind, const Rect2Dd& box,
                             double scale, double& total) {
        std::vector<LaidOut> out;
        total = 0;
        int number = 0, numberLevel = -1;
        bool first = true;
        for (const PptxParagraph& p : paragraphs) {
            const float sizePt = (!p.runs.empty() && p.runs[0].sizePt > 0 ? p.runs[0].sizePt
                                                                           : DefaultSize(kind, p.level));
            const double sizePx = PxOfPoints(sizePt * scale);
            if (p.numbered) {
                number = numberLevel == p.level ? number + 1 : 1;
                numberLevel = p.level;
            } else if (p.level <= numberLevel) {
                numberLevel = -1;
            }
            std::string prefix;
            if (p.bullet) prefix = p.numbered ? std::to_string(number) + ". " : (p.level % 2 ? "\xE2\x80\x93 " : "\xE2\x80\xA2 ");
            std::string text = prefix;
            for (const PptxRun& run : p.runs) text += run.text;
            const double indent = Px(kLevelIndent) * p.level;
            const double width = std::max(4.0, box.width - indent);
            const double before = first ? 0.0 : sizePx * (kind == PptxShapeKind::Body ? 0.25 : 0.1);
            first = false;

            auto layout = ctx_->CreateTextLayout(text.empty() ? std::string(" ") : text, false);
            if (!layout) continue;
            FontStyle font;
            font.fontSize = sizePx * 72.0 / 96.0;   // the layout's points at 96 pixels per inch
            layout->SetFontStyle(font);
            layout->SetWrap(TextWrap::WrapWordChar);
            layout->SetExplicitWidth(width);
            layout->SetAlignment(Alignment(p.align));
            // Each run its own weight, slant, underline, size and colour.
            int start = 0;
            auto apply = [&](int from, int to, const PptxRun& run) {
                if (to <= from) return;
                auto size = TextAttributeFactory::CreateAbsoluteFontSize(
                    static_cast<float>(PxOfPoints((run.sizePt > 0 ? run.sizePt : sizePt) * scale)));
                size->SetRange(from, to);
                layout->InsertAttribute(std::move(size));
                auto colour = TextAttributeFactory::CreateForeground(FromHex(run.color, ink_));
                colour->SetRange(from, to);
                layout->InsertAttribute(std::move(colour));
                if (run.bold) {
                    auto weight = TextAttributeFactory::CreateFontWeight(FontWeight::Bold);
                    weight->SetRange(from, to);
                    layout->InsertAttribute(std::move(weight));
                }
                if (run.italic) {
                    auto slant = TextAttributeFactory::CreateFontStyle(FontSlant::Italic);
                    slant->SetRange(from, to);
                    layout->InsertAttribute(std::move(slant));
                }
                if (run.underline) {
                    auto underline = TextAttributeFactory::CreateUnderline(UCUnderlineType::UnderlineSingle);
                    underline->SetRange(from, to);
                    layout->InsertAttribute(std::move(underline));
                }
            };
            if (!prefix.empty()) {
                PptxRun bulletRun = p.runs.empty() ? PptxRun{} : p.runs[0];
                bulletRun.bold = bulletRun.italic = bulletRun.underline = false;
                apply(0, static_cast<int>(prefix.size()), bulletRun);
                start = static_cast<int>(prefix.size());
            }
            for (const PptxRun& run : p.runs) {
                apply(start, start + static_cast<int>(run.text.size()), run);
                start += static_cast<int>(run.text.size());
            }
            const double height = layout->GetLayoutSize().height;
            LaidOut laid;
            laid.layout = std::move(layout);
            laid.x = box.x + indent;
            laid.top = total + before;
            laid.height = height;
            total = laid.top + height;
            out.push_back(std::move(laid));
        }
        return out;
    }

    double MeasureParagraphs(const std::vector<PptxParagraph>& paragraphs, double width) {
        double total = 0;
        Lay(paragraphs, PptxShapeKind::Text, Rect2Dd(0, 0, std::max(4.0, width), 1), 1.0, total);
        return total;
    }

    void PaintParagraphs(const std::vector<PptxParagraph>& paragraphs, PptxShapeKind kind, const Rect2Dd& box,
                         PptxAnchor anchor, float fontScale, bool shrink) {
        double scale = fontScale > 0 ? fontScale : 1.0;
        double total = 0;
        std::vector<LaidOut> laid = Lay(paragraphs, kind, box, scale, total);
        // Too tall for its box: smaller, down to 40 %.
        while (shrink && total > box.height && scale > 0.4) {
            scale *= 0.9;
            laid = Lay(paragraphs, kind, box, scale, total);
        }
        double y = box.y;
        if (anchor == PptxAnchor::Middle) y += (box.height - total) / 2.0;
        else if (anchor == PptxAnchor::Bottom) y += box.height - total;
        ctx_->PushState();
        ctx_->ClipRect(Rect2Dd(box.x - 2, std::min(box.y, y) - 2, box.width + 4, std::max(box.height, total) + 4));
        for (const LaidOut& l : laid) {
            ctx_->SetTextPaint(ink_);
            ctx_->DrawTextLayout(*l.layout, Point2Dd(l.x, y + l.top));
        }
        ctx_->PopState();
    }
};

} // namespace

std::shared_ptr<UCPixmap> RenderPptxSlide(const PptxPresentation& deck, size_t index, int widthPx) {
    if (index >= deck.slides.size() || widthPx <= 0) return nullptr;
    if (!EnsureImageSubsystem()) return nullptr;
    const int64_t slideW = deck.slideWidth > 0 ? deck.slideWidth : kDefaultSlideWidth;
    const int64_t slideH = deck.slideHeight > 0 ? deck.slideHeight : kDefaultSlideHeight;
    const double scale = static_cast<double>(widthPx) / static_cast<double>(slideW);
    const int w = widthPx;
    const int h = std::max(1, static_cast<int>(std::lround(slideH * scale)));

    // The process-wide font machinery, as the file previews' pages.
    static std::mutex drawing;
    std::lock_guard<std::mutex> lock(drawing);
    auto pixmap = std::make_shared<UCPixmap>();
    if (!pixmap->Init(w, h)) return nullptr;
    std::unique_ptr<IRenderContext> ctx = CreateRenderContext(Size2Di(w, h), nullptr);
    if (!ctx) return nullptr;
    SlidePainter(ctx.get(), deck, deck.slides[index], scale).Paint(w, h);
    ctx->FlushToSurface(pixmap->GetSurface(), Point2Dd(0, 0));
    pixmap->MarkDirty();
    return pixmap;
}

} // namespace UltraCanvas
