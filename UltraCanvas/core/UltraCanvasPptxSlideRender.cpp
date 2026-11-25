// core/UltraCanvasPptxSlideRender.cpp
// A PowerPoint slide drawn as a picture - see UltraCanvasPptxSlideRender.h.
// Version: 1.2.0 - DrawPptxSlide: on any context, into a rectangle; prompt text, a shape's text left out
// Version: 1.1.0 - preset and custom outlines turned and mirrored, gradients, dashes, arrowheads,
//                  connectors, cropped pictures, charts (the chart engine), styled and merged table
//                  cells; text in its fonts with its spacing, indents, insets, bullet characters,
//                  numbering schemes, strike, super- and subscript, caps, highlight and links
// Version: 1.0.0
// Last Modified: 2026-11-20
// Author: UltraCanvas Framework

#include "UltraCanvasPptxSlideRender.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasRichDocumentChart.h"
#include "Documents/Word/UltraCanvasWordFormatInternal.h"   // SymbolFontCharToUnicode

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

namespace UltraCanvas {

namespace {

constexpr double kEmuPerPoint = 12700.0;
constexpr double kDegrees = 3.14159265358979323846 / 180.0;
constexpr int64_t kDefaultSlideWidth = 12192000;   // 16:9
constexpr int64_t kDefaultSlideHeight = 6858000;
constexpr int64_t kInsetX = 91440;                 // a text box's default insets: 0.1", 0.05"
constexpr int64_t kInsetY = 45720;
constexpr int64_t kLevelIndent = 457200;           // 0.5" per outline level, where the deck says none

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
    const std::string digits = !hex.empty() && hex[0] == '#' ? hex.substr(1) : hex;
    if (digits.size() != 6) return fallback;
    const unsigned long rgb = std::strtoul(digits.c_str(), nullptr, 16);
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

// The first code point of UTF-8 `text`; 0 for none.
uint32_t FirstCodepoint(const std::string& text) {
    if (text.empty()) return 0;
    const auto* s = reinterpret_cast<const unsigned char*>(text.data());
    const size_t n = text.size();
    if (s[0] < 0x80) return s[0];
    if ((s[0] & 0xE0) == 0xC0 && n >= 2) return ((s[0] & 0x1Fu) << 6) | (s[1] & 0x3Fu);
    if ((s[0] & 0xF0) == 0xE0 && n >= 3) return ((s[0] & 0x0Fu) << 12) | ((s[1] & 0x3Fu) << 6) | (s[2] & 0x3Fu);
    if ((s[0] & 0xF8) == 0xF0 && n >= 4)
        return ((s[0] & 0x07u) << 18) | ((s[1] & 0x3Fu) << 12) | ((s[2] & 0x3Fu) << 6) | (s[3] & 0x3Fu);
    return 0;
}

std::string Utf8(uint32_t cp) {
    std::string out;
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return out;
}

// A bullet character as it shows: a symbol font's code (Wingdings' "§" is a
// square) as the Unicode character it draws.
std::string BulletText(const PptxParagraph& p) {
    if (p.bulletChar.empty()) return p.level % 2 ? "\xE2\x80\x93" : "\xE2\x80\xA2";   // – / •
    const uint32_t cp = FirstCodepoint(p.bulletChar);
    if (!p.bulletFont.empty() && cp) {
        if (const uint32_t mapped = WordFormatInternal::SymbolFontCharToUnicode(p.bulletFont, cp)) return Utf8(mapped);
    }
    return p.bulletChar;
}

// ASCII letters in capitals, byte for byte (so attribute ranges stay put).
std::string AsciiUpper(std::string text) {
    for (char& c : text)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return text;
}

// The outline a shape is drawn in: its custom path, else its preset.
RichShape OutlineOf(const PptxShape& shape) {
    RichShape outline;
    if (!shape.customPath.empty()) {
        outline.kind = RichShape::Kind::Path;
        outline.path = shape.customPath;
    } else if (!outline.ApplyPreset(shape.geometry.empty() ? "rect" : shape.geometry)) {
        outline.kind = RichShape::Kind::Rectangle;
    }
    if (shape.kind == PptxShapeKind::Connector && outline.kind != RichShape::Kind::Path) outline.kind = RichShape::Kind::Line;
    return outline;
}

void Trace(IRenderContext* ctx, const RichShape& shape, const Rect2Dd& box) {
    using Kind = RichShape::Kind;
    using Op = RichShape::PathPoint::Op;
    const double x = box.x, y = box.y, w = box.width, h = box.height;
    ctx->ClearPath();
    switch (shape.kind) {
        case Kind::Rectangle:
        case Kind::Group:
            ctx->Rect(x, y, w, h);
            break;
        case Kind::RoundRectangle: {
            const double radius = std::clamp(static_cast<double>(shape.cornerRadius), 0.0, 0.5) * std::min(w, h);
            if (radius > 0.0) ctx->RoundedRect(x, y, w, h, radius);
            else ctx->Rect(x, y, w, h);
            break;
        }
        case Kind::Ellipse:
            if (w > 0.0 && h > 0.0) ctx->Ellipse(x + w * 0.5, y + h * 0.5, w * 0.5, h * 0.5, 0.0);
            break;
        case Kind::Line:
            ctx->MoveTo(x, y);
            ctx->LineTo(x + w, y + h);
            break;
        case Kind::Path:
            for (const RichShape::PathPoint& point : shape.path) {
                switch (point.op) {
                    case Op::Move: ctx->MoveTo(x + point.x * w, y + point.y * h); break;
                    case Op::Line: ctx->LineTo(x + point.x * w, y + point.y * h); break;
                    case Op::Curve:
                        ctx->BezierCurveTo(x + point.c1x * w, y + point.c1y * h, x + point.c2x * w, y + point.c2y * h,
                                           x + point.x * w, y + point.y * h);
                        break;
                    case Op::Close: ctx->ClosePath(); break;
                }
            }
            break;
    }
}

void Arrowhead(IRenderContext* ctx, double fromX, double fromY, double tipX, double tipY, double lineWidth,
               const Color& color) {
    const double dx = tipX - fromX, dy = tipY - fromY;
    const double length = std::hypot(dx, dy);
    if (length < 0.01) return;
    const double ux = dx / length, uy = dy / length;
    const double size = std::max(5.0, lineWidth * 3.5);
    const double baseX = tipX - ux * size, baseY = tipY - uy * size;
    ctx->ClearPath();
    ctx->MoveTo(tipX, tipY);
    ctx->LineTo(baseX - uy * size * 0.5, baseY + ux * size * 0.5);
    ctx->LineTo(baseX + uy * size * 0.5, baseY - ux * size * 0.5);
    ctx->ClosePath();
    ctx->SetFillPaint(color);
    ctx->Fill();
}

class SlidePainter {
public:
    SlidePainter(IRenderContext* ctx, const PptxPresentation& deck, const PptxSlide& slide, double scale,
                 const PptxSlideDrawOptions* options = nullptr)
        : ctx_(ctx), deck_(deck), slide_(slide), scale_(scale), options_(options),
          ink_(FromHex(slide.textColor, Colors::Black)),
          link_(FromHex(ValueOr(deck.themeColors, "hlink", "0563C1"), Color(5, 99, 193))) {}

    // `clear`: the whole context takes the background (a pixmap of the
    // slide); else only the slide's rectangle is filled.
    void Paint(double width, double height, bool clear = true) {
        width_ = width;
        height_ = height;
        if (clear) {
            ctx_->Clear(FromHex(slide_.background, Colors::White));
        } else {
            ctx_->SetFillPaint(FromHex(slide_.background, Colors::White));
            ctx_->FillRectangle(Rect2Dd(0, 0, width, height));
        }
        for (size_t index = 0; index < slide_.shapes.size(); ++index) {
            const PptxShape& shape = ShapeToPaint(index);
            const Rect2Dd box = BoxOf(shape);
            if (shape.kind == PptxShapeKind::Connector) {
                PaintGeometry(shape, box);
                continue;
            }
            if (box.width <= 1 || box.height <= 1) continue;
            switch (shape.kind) {
                case PptxShapeKind::Picture: PaintPicture(shape, box); break;
                case PptxShapeKind::Table: PaintTable(shape, box); break;
                case PptxShapeKind::Chart: PaintChart(shape, box); break;
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
    const PptxSlideDrawOptions* options_;
    PptxShape substitute_;   // a shape drawn with its prompt text, or without its text

    static bool HasText(const PptxShape& shape) {
        for (const PptxParagraph& paragraph : shape.paragraphs)
            for (const PptxRun& run : paragraph.runs)
                if (!run.text.empty()) return true;
        return false;
    }

    // Shape `index` as it is drawn: its text left out where an editor shows
    // it, an empty placeholder's prompt in grey.
    const PptxShape& ShapeToPaint(size_t index) {
        const PptxShape& shape = slide_.shapes[index];
        if (!options_) return shape;
        if (options_->hideTextOfShape == static_cast<int>(index)) {
            substitute_ = shape;
            substitute_.paragraphs.clear();
            return substitute_;
        }
        if (!options_->promptText || shape.placeholder.empty() || HasText(shape)) return shape;
        const std::string prompt = options_->promptText(shape, index);
        if (prompt.empty()) return shape;
        substitute_ = shape;
        PptxParagraph paragraph = shape.paragraphs.empty() ? PptxParagraph() : shape.paragraphs.front();
        PptxRun run = paragraph.runs.empty() ? PptxRun() : paragraph.runs.front();
        run.text = prompt;
        run.color = "8C8C8C";
        paragraph.runs = {run};
        paragraph.bullet = false;
        paragraph.numbered = false;
        substitute_.paragraphs = {paragraph};
        return substitute_;
    }
    Color ink_;
    Color link_;
    double width_ = 0, height_ = 0;

    double Px(int64_t emu) const { return static_cast<double>(emu) * scale_; }
    double PxOfPoints(double points) const { return points * kEmuPerPoint * scale_; }

    static std::string ValueOr(const std::map<std::string, std::string>& map, const std::string& key,
                               const std::string& fallback) {
        const auto found = map.find(key);
        return found == map.end() || found->second.empty() ? fallback : found->second;
    }

    Rect2Dd BoxOf(const PptxShape& shape) const {
        if (shape.kind == PptxShapeKind::Connector || (shape.width > 0 && shape.height > 0))
            return Rect2Dd(Px(shape.x), Px(shape.y), Px(shape.width), Px(shape.height));
        if (shape.kind == PptxShapeKind::Text || shape.kind == PptxShapeKind::Picture ||
            shape.kind == PptxShapeKind::Table || shape.kind == PptxShapeKind::Chart)
            return Rect2Dd(0, 0, 0, 0);
        const Rect2Dd part = FallbackBox(shape.kind);
        return Rect2Dd(part.x * width_, part.y * height_, part.width * width_, part.height * height_);
    }

    // Turned about the box's centre and mirrored, as the shape says; the
    // caller pops the state.
    void PushTransform(const PptxShape& shape, const Rect2Dd& box, bool mirror = true) {
        ctx_->PushState();
        const bool flip = mirror && (shape.flipH || shape.flipV);
        if (shape.rotationDeg == 0 && !flip) return;
        const double cx = box.x + box.width * 0.5, cy = box.y + box.height * 0.5;
        ctx_->Translate(cx, cy);
        if (shape.rotationDeg != 0) ctx_->Rotate(shape.rotationDeg * kDegrees);
        if (flip) ctx_->Scale(shape.flipH ? -1.0 : 1.0, shape.flipV ? -1.0 : 1.0);
        ctx_->Translate(-cx, -cy);
    }

    void PaintGeometry(const PptxShape& shape, const Rect2Dd& box) {
        if (shape.fill.empty() && shape.line.empty()) return;
        const RichShape outline = OutlineOf(shape);
        PushTransform(shape, box);
        Trace(ctx_, outline, box);
        if (!shape.fill.empty() && outline.IsClosed()) {
            if (shape.gradientStops.size() >= 2) {
                std::vector<GradientStop> stops;
                for (const RichShape::GradientStop& stop : shape.gradientStops)
                    stops.emplace_back(std::clamp(static_cast<double>(stop.position), 0.0, 1.0),
                                       FromHex(stop.color, Colors::Transparent));
                std::shared_ptr<IPaintPattern> pattern;
                const double cx = box.x + box.width * 0.5, cy = box.y + box.height * 0.5;
                if (shape.gradientRadial) {
                    const double reach = std::hypot(box.width, box.height) * 0.5;
                    pattern = ctx_->CreateRadialGradientPattern(cx, cy, 0.0, cx, cy, std::max(1.0, reach), stops);
                } else {
                    const double angle = shape.gradientAngleDeg * kDegrees;
                    const double ux = std::cos(angle), uy = std::sin(angle);
                    const double half = (std::abs(box.width * ux) + std::abs(box.height * uy)) * 0.5;
                    pattern = ctx_->CreateLinearGradientPattern(cx - ux * half, cy - uy * half, cx + ux * half,
                                                                cy + uy * half, stops);
                }
                if (pattern) ctx_->SetFillPaint(pattern);
                else ctx_->SetFillPaint(FromHex(shape.fill, Colors::Transparent));
            } else {
                ctx_->SetFillPaint(FromHex(shape.fill, Colors::Transparent));
            }
            ctx_->FillPathPreserve();
        }
        if (!shape.line.empty()) {
            const double lineWidth = std::max(1.0, PxOfPoints(shape.lineWidthPt));
            const Color colour = FromHex(shape.line, Colors::Black);
            ctx_->SetStrokePaint(colour);
            ctx_->SetStrokeWidth(lineWidth);
            if (shape.dash == RichShape::Dash::Dashed) ctx_->SetLineDash(UCDashPattern({lineWidth * 4.0, lineWidth * 3.0}));
            else if (shape.dash == RichShape::Dash::Dotted) ctx_->SetLineDash(UCDashPattern({lineWidth, lineWidth * 2.0}));
            ctx_->StrokePathPreserve();
            ctx_->SetLineDash(UCDashPattern());
            PaintArrowheads(shape, outline, box, lineWidth, colour);
        }
        ctx_->ClearPath();
        ctx_->PopState();
    }

    void PaintArrowheads(const PptxShape& shape, const RichShape& outline, const Rect2Dd& box, double lineWidth,
                         const Color& colour) {
        if (!shape.arrowStart && !shape.arrowEnd) return;
        auto at = [&](float px, float py) { return Point2Dd(box.x + px * box.width, box.y + py * box.height); };
        Point2Dd first, second, beforeLast, last;
        if (outline.kind == RichShape::Kind::Line) {
            first = beforeLast = at(0, 0);
            second = last = at(1, 1);
        } else if (outline.kind == RichShape::Kind::Path && outline.path.size() >= 2 && outline.IsOpen()) {
            const auto& path = outline.path;
            first = at(path[0].x, path[0].y);
            const auto& p1 = path[1];
            second = p1.op == RichShape::PathPoint::Op::Curve ? at(p1.c1x, p1.c1y) : at(p1.x, p1.y);
            const auto& pl = path.back();
            last = at(pl.x, pl.y);
            beforeLast = pl.op == RichShape::PathPoint::Op::Curve ? at(pl.c2x, pl.c2y)
                                                                  : at(path[path.size() - 2].x, path[path.size() - 2].y);
        } else {
            return;
        }
        if (shape.arrowStart) Arrowhead(ctx_, second.x, second.y, first.x, first.y, lineWidth, colour);
        if (shape.arrowEnd) Arrowhead(ctx_, beforeLast.x, beforeLast.y, last.x, last.y, lineWidth, colour);
    }

    void PaintPicture(const PptxShape& shape, const Rect2Dd& box) {
        const RichShape outline = OutlineOf(shape);
        PushTransform(shape, box);
        const auto bytes = deck_.pictures.find(shape.pictureTarget);
        bool drawn = false;
        if (bytes != deck_.pictures.end() && !bytes->second.empty()) {
            if (auto image = UCImage::LoadFromMemory(bytes->second)) {
                // Cropped: the whole picture laid out so its kept part fills
                // the box, clipped to the shape's outline.
                const double keepW = std::max(0.01, 1.0 - shape.cropLeft - shape.cropRight);
                const double keepH = std::max(0.01, 1.0 - shape.cropTop - shape.cropBottom);
                const double fullW = box.width / keepW, fullH = box.height / keepH;
                const Rect2Dd whole(box.x - shape.cropLeft * fullW, box.y - shape.cropTop * fullH, fullW, fullH);
                ctx_->PushState();
                Trace(ctx_, outline.kind == RichShape::Kind::Line ? RichShape{} : outline, box);
                ctx_->ClipPath();
                ctx_->DrawImage(*image, whole, ImageFitMode::Fill);
                ctx_->PopState();
                drawn = true;
            }
        }
        if (!drawn) {
            // Not read or not decodable: where it stands, and what it shows.
            ctx_->DrawFilledRectangle(box, Color(236, 238, 241), 1.0f, Color(200, 204, 210));
            if (!shape.altText.empty()) {
                PptxParagraph p;
                p.align = PptxAlign::Center;
                p.runs.push_back({shape.altText, false, true, false, 14, "6B7280"});
                PaintParagraphs({p}, PptxShapeKind::Text, Rect2Dd(box.x + 6, box.y, box.width - 12, box.height),
                                PptxAnchor::Middle, 1.0f, false, true);
            }
        }
        if (!shape.line.empty()) {
            Trace(ctx_, outline, box);
            ctx_->SetStrokePaint(FromHex(shape.line, Colors::Black));
            ctx_->SetStrokeWidth(std::max(1.0, PxOfPoints(shape.lineWidthPt)));
            ctx_->Stroke();
        }
        ctx_->PopState();
    }

    void PaintChart(const PptxShape& shape, const Rect2Dd& box) {
        // Laid out at its own size on the slide (96 pixels an inch) and scaled
        // into the box, so its words keep their proportion at any width.
        const Size2Di natural(std::max(40, static_cast<int>(std::lround(shape.width / kEmuPerPoint * 96.0 / 72.0))),
                              std::max(40, static_cast<int>(std::lround(shape.height / kEmuPerPoint * 96.0 / 72.0))));
        if (DrawRichDocumentChart(ctx_, shape.chart, box, natural)) return;
        // A build without the chart engine: where it stands, and its title.
        ctx_->DrawFilledRectangle(box, Color(246, 247, 249), 1.0f, Color(200, 204, 210));
        PptxParagraph p;
        p.align = PptxAlign::Center;
        p.runs.push_back({shape.chart.title.empty() ? std::string("Chart") : shape.chart.title, false, true, false, 14,
                          "6B7280"});
        PaintParagraphs({p}, PptxShapeKind::Text, box, PptxAnchor::Middle, 1.0f, false, true);
    }

    // ----- Tables -----

    void PaintTable(const PptxShape& shape, const Rect2Dd& box) {
        if (shape.rows.empty()) return;
        size_t columns = shape.columnWidths.size();
        for (const auto& row : shape.rows) columns = std::max(columns, row.size());
        if (columns == 0) return;
        std::vector<double> widths(columns, box.width / static_cast<double>(columns));
        if (shape.columnWidths.size() == columns) {
            for (size_t c = 0; c < columns; ++c) widths[c] = Px(shape.columnWidths[c]);
        }
        const bool styled = shape.cellStyles.size() == shape.rows.size();
        auto styleOf = [&](size_t r, size_t c) -> const PptxCellStyle* {
            if (!styled || c >= shape.cellStyles[r].size()) return nullptr;
            return &shape.cellStyles[r][c];
        };
        // Without styles (a deck from another reader): PowerPoint's default look.
        const Color accent = FromHex(ValueOr(deck_.themeColors, "accent1", "4472C4"), Color(68, 114, 196));
        const Color band1 = Mix(accent, Colors::White, 0.8), band2 = Mix(accent, Colors::White, 0.9);

        // Each row as tall as it asks, or as its unmerged cells' text needs.
        std::vector<double> heights(shape.rows.size(), 0.0);
        for (size_t r = 0; r < shape.rows.size(); ++r) {
            heights[r] = r < shape.rowHeights.size() ? Px(shape.rowHeights[r]) : 0.0;
            for (size_t c = 0; c < shape.rows[r].size() && c < columns; ++c) {
                const PptxCellStyle* look = styleOf(r, c);
                double width = widths[c];
                if (look) {
                    if (look->mergedAway || look->rowSpan > 1) continue;
                    for (int extra = 1; extra < look->gridSpan && c + extra < columns; ++extra) width += widths[c + extra];
                }
                const double insetX = look ? Px(look->insetLeft + look->insetRight) : 2 * Px(kInsetX);
                const double insetY = look ? Px(look->insetTop + look->insetBottom) : 2 * Px(kInsetY);
                const double need = MeasureParagraphs(shape.rows[r][c], width - insetX);
                heights[r] = std::max(heights[r], need + insetY);
            }
        }
        std::vector<double> lefts(columns + 1, box.x);
        for (size_t c = 0; c < columns; ++c) lefts[c + 1] = lefts[c] + widths[c];
        std::vector<double> tops(shape.rows.size() + 1, box.y);
        for (size_t r = 0; r < shape.rows.size(); ++r) tops[r + 1] = tops[r] + heights[r];

        for (size_t r = 0; r < shape.rows.size(); ++r) {
            for (size_t c = 0; c < columns; ++c) {
                const PptxCellStyle* look = styleOf(r, c);
                if (look && look->mergedAway) continue;
                const size_t spanC = look ? std::min<size_t>(columns - c, static_cast<size_t>(look->gridSpan)) : 1;
                const size_t spanR = look ? std::min<size_t>(shape.rows.size() - r, static_cast<size_t>(look->rowSpan)) : 1;
                const Rect2Dd cell(lefts[c], tops[r], lefts[c + spanC] - lefts[c], tops[r + spanR] - tops[r]);
                const bool header = r == 0;
                if (look) {
                    if (!look->fill.empty()) {
                        ctx_->SetFillPaint(FromHex(look->fill, Colors::Transparent));
                        ctx_->FillRectangle(cell);
                    }
                    PaintBorder(look->top, cell.x, cell.y, cell.x + cell.width, cell.y);
                    PaintBorder(look->bottom, cell.x, cell.y + cell.height, cell.x + cell.width, cell.y + cell.height);
                    PaintBorder(look->left, cell.x, cell.y, cell.x, cell.y + cell.height);
                    PaintBorder(look->right, cell.x + cell.width, cell.y, cell.x + cell.width, cell.y + cell.height);
                } else {
                    ctx_->DrawFilledRectangle(cell, header ? accent : (r % 2 ? band1 : band2), 1.0f, Colors::White);
                }
                if (c >= shape.rows[r].size()) continue;
                std::vector<PptxParagraph> text = shape.rows[r][c];
                if (!look && header) {
                    for (PptxParagraph& p : text)
                        for (PptxRun& run : p.runs) {
                            run.bold = true;
                            if (run.color.empty()) run.color = "FFFFFF";
                        }
                }
                Rect2Dd inner = look ? Rect2Dd(cell.x + Px(look->insetLeft), cell.y + Px(look->insetTop),
                                               std::max(1.0, cell.width - Px(look->insetLeft + look->insetRight)),
                                               std::max(1.0, cell.height - Px(look->insetTop + look->insetBottom)))
                                     : Rect2Dd(cell.x + Px(kInsetX), cell.y + Px(kInsetY),
                                               std::max(1.0, cell.width - 2 * Px(kInsetX)),
                                               std::max(1.0, cell.height - 2 * Px(kInsetY)));
                PaintParagraphs(text, PptxShapeKind::Text, inner, look ? look->anchor : PptxAnchor::Middle, 1.0f, false, true);
            }
        }
    }

    void PaintBorder(const PptxCellStyle::Border& border, double x1, double y1, double x2, double y2) {
        if (border.color.empty() || border.widthPt <= 0) return;
        ctx_->ClearPath();
        ctx_->MoveTo(x1, y1);
        ctx_->LineTo(x2, y2);
        ctx_->SetStrokePaint(FromHex(border.color, Colors::Black));
        ctx_->SetStrokeWidth(std::max(1.0, PxOfPoints(border.widthPt)));
        ctx_->Stroke();
    }

    // ----- Text -----

    void PaintText(const PptxShape& shape, const Rect2Dd& box) {
        bool any = false;
        for (const PptxParagraph& p : shape.paragraphs) any = any || !p.Text().empty();
        if (!any) return;
        const double left = Px(shape.insetLeft >= 0 ? shape.insetLeft : kInsetX);
        const double right = Px(shape.insetRight >= 0 ? shape.insetRight : kInsetX);
        const double top = Px(shape.insetTop >= 0 ? shape.insetTop : kInsetY);
        const double bottom = Px(shape.insetBottom >= 0 ? shape.insetBottom : kInsetY);
        // Placeholders shrink to fit, as PowerPoint's autofit does.
        const bool shrink = !shape.placeholder.empty() || shape.fontScale < 1.0f;
        // Turned with its shape (never mirrored); vertical text a quarter more.
        ctx_->PushState();
        const double cx = box.x + box.width * 0.5, cy = box.y + box.height * 0.5;
        const double turn = shape.rotationDeg + shape.textRotation + (shape.flipV ? 180.0 : 0.0);
        Rect2Dd frame = box;
        if (turn != 0) {
            ctx_->Translate(cx, cy);
            ctx_->Rotate(turn * kDegrees);
            ctx_->Translate(-cx, -cy);
        }
        if (shape.textRotation == 90 || shape.textRotation == 270)
            frame = Rect2Dd(cx - box.height * 0.5, cy - box.width * 0.5, box.height, box.width);
        const Rect2Dd inner(frame.x + left, frame.y + top, std::max(1.0, frame.width - left - right),
                            std::max(1.0, frame.height - top - bottom));
        PaintParagraphs(shape.paragraphs, shape.kind, inner, shape.anchor, shape.fontScale, shrink, shape.wrap);
        ctx_->PopState();
    }

    struct LaidOut {
        std::unique_ptr<ITextLayout> layout;
        std::unique_ptr<ITextLayout> bullet;   // a hanging bullet or number, drawn apart
        double x = 0, bulletX = 0, top = 0, height = 0;
    };

    // A run's attributes over [from, to) of `layout`.
    void Attribute(ITextLayout& layout, int from, int to, const PptxRun& run, float sizePt, double scale) {
        if (to <= from) return;
        auto ranged = [&](std::unique_ptr<ITextAttribute> attribute) {
            attribute->SetRange(from, to);
            layout.InsertAttribute(std::move(attribute));
        };
        double size = (run.sizePt > 0 ? run.sizePt : sizePt) * scale;
        if (run.baseline != 0) {
            size *= 0.67;   // set smaller and raised or lowered, as PowerPoint does
            ranged(TextAttributeFactory::CreateRise(static_cast<int>(
                std::lround(PxOfPoints((run.sizePt > 0 ? run.sizePt : sizePt) * scale) * run.baseline / 100.0))));
        }
        ranged(TextAttributeFactory::CreateAbsoluteFontSize(static_cast<float>(PxOfPoints(size))));
        if (!run.font.empty()) ranged(TextAttributeFactory::CreateFontFamily(run.font));
        const bool linked = !run.hyperlink.empty();
        ranged(TextAttributeFactory::CreateForeground(linked ? link_ : FromHex(run.color, ink_)));
        if (run.bold) ranged(TextAttributeFactory::CreateFontWeight(FontWeight::Bold));
        if (run.italic) ranged(TextAttributeFactory::CreateFontStyle(FontSlant::Italic));
        if (run.underline || linked) ranged(TextAttributeFactory::CreateUnderline(UCUnderlineType::UnderlineSingle));
        if (run.strike) ranged(TextAttributeFactory::CreateStrikethrough(true));
        if (run.smallCaps) ranged(TextAttributeFactory::CreateVariant(UCFontVariant::VariantSmallCaps));
        if (!run.highlight.empty()) ranged(TextAttributeFactory::CreateBackground(FromHex(run.highlight, Colors::Transparent)));
    }

    // Spacing before or after a paragraph whose first run is `sizePx` high.
    double SpacingPx(const PptxSpacing& spacing, double sizePx, double scale) const {
        if (spacing.points > 0) return PxOfPoints(spacing.points * scale);
        if (spacing.percent > 0) return spacing.percent * sizePx * 1.2;
        return 0.0;
    }

    // The paragraphs set into `box` at `scale` of their sizes; their total height.
    std::vector<LaidOut> Lay(const std::vector<PptxParagraph>& paragraphs, PptxShapeKind kind, const Rect2Dd& box,
                             double scale, double& total, bool wrap) {
        std::vector<LaidOut> out;
        total = 0;
        int numbers[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
        bool first = true;
        double afterPrevious = 0;
        for (const PptxParagraph& p : paragraphs) {
            const int level = std::clamp(p.level, 0, 8);
            const float sizePt = (!p.runs.empty() && p.runs[0].sizePt > 0 ? p.runs[0].sizePt : DefaultSize(kind, level));
            const double sizePx = PxOfPoints(sizePt * scale);
            // Numbering: counted per level, a deeper paragraph keeps the count, a
            // shallower or unnumbered one at the level starts it again.
            std::string label;
            if (p.numbered) {
                label = PptxNumberLabel(p.numberScheme, p.numberStartAt + numbers[level]);
                ++numbers[level];
                for (int deeper = level + 1; deeper < 9; ++deeper) numbers[deeper] = 0;
            } else {
                for (int at = level; at < 9; ++at) numbers[at] = 0;
                if (p.bullet) label = BulletText(p);
            }
            const bool hasLabel = p.bullet && !label.empty();
            const double marginLeft = p.marginLeft >= 0 ? Px(p.marginLeft) : Px(kLevelIndent) * level;
            const double indent = Px(p.indent);
            const bool hanging = hasLabel && indent < 0;
            const double textX = box.x + marginLeft;
            const double width = std::max(4.0, box.width - marginLeft);
            double before = SpacingPx(p.spaceBefore, sizePx, scale);
            if (!p.spaceBefore.IsSet() && kind == PptxShapeKind::Body) before = sizePx * 0.2;
            if (first) before = 0;
            before = std::max(before, afterPrevious);
            first = false;

            std::string text;
            for (const PptxRun& run : p.runs) text += run.allCaps ? AsciiUpper(run.text) : run.text;
            const std::string prefix = hasLabel && !hanging ? label + " " : std::string();
            auto layout = ctx_->CreateTextLayout(prefix + text + (text.empty() ? " " : ""), false);
            if (!layout) continue;
            FontStyle font;
            font.fontSize = sizePx * 72.0 / 96.0;   // the layout's points at 96 pixels per inch
            if (!p.runs.empty() && !p.runs[0].font.empty()) font.fontFamily = p.runs[0].font;
            layout->SetFontStyle(font);
            layout->SetWrap(wrap ? TextWrap::WrapWordChar : TextWrap::WrapNone);
            layout->SetExplicitWidth(wrap ? width : -1);
            layout->SetAlignment(Alignment(p.align));
            if (!hasLabel && indent != 0) layout->SetIndent(static_cast<int>(std::lround(indent)));
            if (p.lineSpacing.percent > 0) {
                layout->SetLineSpacing(p.lineSpacing.percent);
            } else if (p.lineSpacing.points > 0 && sizePx > 0) {
                layout->SetLineSpacing(static_cast<float>(PxOfPoints(p.lineSpacing.points * scale) / (sizePx * 1.2)));
            }
            int start = 0;
            if (!prefix.empty()) {
                PptxRun bulletRun = p.runs.empty() ? PptxRun{} : p.runs[0];
                bulletRun.bold = bulletRun.italic = bulletRun.underline = bulletRun.strike = false;
                bulletRun.hyperlink.clear();
                bulletRun.baseline = 0;
                if (!p.bulletColor.empty()) bulletRun.color = p.bulletColor;
                if (p.bulletSizePercent > 0) bulletRun.sizePt = (bulletRun.sizePt > 0 ? bulletRun.sizePt : sizePt) * p.bulletSizePercent / 100.0f;
                Attribute(*layout, 0, static_cast<int>(prefix.size()), bulletRun, sizePt, scale);
                start = static_cast<int>(prefix.size());
            }
            for (const PptxRun& run : p.runs) {
                Attribute(*layout, start, start + static_cast<int>(run.text.size()), run, sizePt, scale);
                start += static_cast<int>(run.text.size());
            }
            LaidOut laid;
            if (hanging) {
                // The label hangs in the indent, the text keeps its margin.
                laid.bullet = ctx_->CreateTextLayout(label, false);
                if (laid.bullet) {
                    PptxRun bulletRun = p.runs.empty() ? PptxRun{} : p.runs[0];
                    bulletRun.bold = bulletRun.italic = bulletRun.underline = bulletRun.strike = false;
                    bulletRun.hyperlink.clear();
                    bulletRun.baseline = 0;
                    if (!p.bulletColor.empty()) bulletRun.color = p.bulletColor;
                    if (p.bulletSizePercent > 0) bulletRun.sizePt = (bulletRun.sizePt > 0 ? bulletRun.sizePt : sizePt) * p.bulletSizePercent / 100.0f;
                    laid.bullet->SetFontStyle(font);
                    laid.bullet->SetWrap(TextWrap::WrapNone);
                    Attribute(*laid.bullet, 0, static_cast<int>(label.size()), bulletRun, sizePt, scale);
                    laid.bulletX = textX + indent;
                }
            }
            const double height = layout->GetLayoutSize().height;
            laid.layout = std::move(layout);
            laid.x = textX;
            if (!wrap && p.align != PptxAlign::Left) {
                // One line as wide as it needs, aligned in the box.
                const double natural = laid.layout->GetLayoutSize().width;
                laid.x = p.align == PptxAlign::Center ? box.x + (box.width - natural) / 2.0
                       : p.align == PptxAlign::Right ? box.x + box.width - natural : textX;
            }
            laid.top = total + before;
            laid.height = height;
            total = laid.top + height;
            afterPrevious = SpacingPx(p.spaceAfter, sizePx, scale);
            out.push_back(std::move(laid));
        }
        return out;
    }

    double MeasureParagraphs(const std::vector<PptxParagraph>& paragraphs, double width) {
        double total = 0;
        Lay(paragraphs, PptxShapeKind::Text, Rect2Dd(0, 0, std::max(4.0, width), 1), 1.0, total, true);
        return total;
    }

    void PaintParagraphs(const std::vector<PptxParagraph>& paragraphs, PptxShapeKind kind, const Rect2Dd& box,
                         PptxAnchor anchor, float fontScale, bool shrink, bool wrap) {
        double scale = fontScale > 0 ? fontScale : 1.0;
        double total = 0;
        std::vector<LaidOut> laid = Lay(paragraphs, kind, box, scale, total, wrap);
        // Too tall for its box: smaller, down to 40 %.
        while (shrink && total > box.height && scale > 0.4) {
            scale *= 0.9;
            laid = Lay(paragraphs, kind, box, scale, total, wrap);
        }
        double y = box.y;
        if (anchor == PptxAnchor::Middle) y += (box.height - total) / 2.0;
        else if (anchor == PptxAnchor::Bottom) y += box.height - total;
        ctx_->PushState();
        if (wrap)
            ctx_->ClipRect(Rect2Dd(box.x - 2, std::min(box.y, y) - 2, box.width + 4, std::max(box.height, total) + 4));
        for (const LaidOut& l : laid) {
            ctx_->SetTextPaint(ink_);
            if (l.bullet) ctx_->DrawTextLayout(*l.bullet, Point2Dd(l.bulletX, y + l.top));
            ctx_->DrawTextLayout(*l.layout, Point2Dd(l.x, y + l.top));
        }
        ctx_->PopState();
    }
};

} // namespace

void DrawPptxSlide(IRenderContext* ctx, const PptxPresentation& deck, const PptxSlide& slide, const Rect2Dd& rect,
                   const PptxSlideDrawOptions& options) {
    if (!ctx || rect.width <= 0 || rect.height <= 0) return;
    const int64_t slideW = deck.slideWidth > 0 ? deck.slideWidth : kDefaultSlideWidth;
    const double scale = rect.width / static_cast<double>(slideW);
    ctx->PushState();
    ctx->Translate(rect.x, rect.y);
    ctx->ClipRect(Rect2Dd(0, 0, rect.width, rect.height));
    SlidePainter(ctx, deck, slide, scale, &options).Paint(rect.width, rect.height, false);
    ctx->PopState();
}

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
