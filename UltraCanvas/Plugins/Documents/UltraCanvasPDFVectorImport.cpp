// Plugins/Documents/UltraCanvasPDFVectorImport.cpp
// ImportPDFAsVectorDocument (UltraCanvasPDF.h): one page of a PDF as an
// editable drawing. MuPDF interprets the page and hands every operation to a
// device of ours, which builds the VectorDocument as it goes:
//
//   - the page format: the page as a viewer shows it - its crop box, turned
//     by /Rotate and scaled by /UserUnit (fz_bound_page) - becomes the
//     document's Size and ViewBox, so the drawing opens on the PDF's own page
//   - paths, filled and stroked (a fill and a stroke of the same path, which
//     is how PDF's B operator reaches a device, become one shape)
//   - text, a VectorText per line with a span per word placed where the PDF
//     put it, so a substituted font cannot push a line out of its column;
//     text without Unicode, and outlined text, as the glyphs' outlines
//   - images: a JPEG or PNG stream embedded as it is in the file (no
//     re-encoding of a 30000-pixel poster), anything else - masked, CMYK,
//     Flate, stencil masks - drawn to RGBA and embedded as PNG
//   - clipping paths (clip-path definitions), clipping text (its outlines),
//     transparency groups (opacity, blend mode), tiling patterns (the cell
//     repeated over the area it fills)
//   - shadings: drawn to a picture of the region they fill
//
// Soft masks have no counterpart in the model: what they mask is kept
// unmasked, and the reader says so. Built only with MuPDF
// (ULTRACANVAS_PDF_MUPDF); MuPDF stays private to this library, so the
// Vector plugin reaches it through this function and never includes fitz.h.
//
// Version: 1.0.1 - builds against MuPDF after 1.23 too (end_mask, begin_tile)
// Version: 1.0.0
// Last Modified: 2026-11-28
// Author: UltraCanvas Framework

#include "Plugins/Documents/UltraCanvasPDF.h"

#ifdef ULTRACANVAS_PDF_MUPDF

#include "DataFormats/UltraCanvasVectorStorage.h"
#include "UltraCanvasTextUtils.h"   // Base64Encode

extern "C" {
#include <mupdf/fitz.h>
}

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

namespace UltraCanvas {

using namespace VectorStorage;

namespace {

// Beyond this many copies a tiling pattern is cut short (and said so): a
// hairline hatch over a whole poster would otherwise be a million shapes.
constexpr long kMaxTileCopies = 4000;
// A shading is drawn at this resolution, and at most this many pixels.
constexpr float kShadeDpi = 150.0f;
constexpr double kMaxShadePixels = 4.0e6;

Matrix3x3 ToMatrix(const fz_matrix& m) {
    // fz_matrix: x' = a x + c y + e, y' = b x + d y + f.
    return Matrix3x3::FromValues(m.a, m.c, m.b, m.d, m.e, m.f);
}

uint8_t ToByte(float v) {
    return static_cast<uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
}

void AppendUtf8(std::string& out, int c) {
    if (c <= 0) return;
    if (c < 0x80) { out.push_back(static_cast<char>(c)); return; }
    if (c < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (c >> 6)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        return;
    }
    if (c < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (c >> 12)));
        out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        return;
    }
    out.push_back(static_cast<char>(0xF0 | (c >> 18)));
    out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
}

bool Contains(const std::string& s, const char* what) { return s.find(what) != std::string::npos; }

// "ABCDEF+Helvetica-BoldOblique" -> family "Helvetica", bold, italic.
// "ArialMT" -> "Arial", "TimesNewRomanPS-ItalicMT" -> "TimesNewRoman".
VectorTextStyle FontStyleOf(fz_context* ctx, fz_font* font) {
    VectorTextStyle style;
    std::string name = font ? fz_font_name(ctx, font) : "";
    const size_t plus = name.find('+');
    if (plus == 6) name.erase(0, 7);   // a subset's tag
    std::string lower = name;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const bool bold = (font && fz_font_is_bold(ctx, font)) || Contains(lower, "bold") || Contains(lower, "black") ||
                      Contains(lower, "heavy") || Contains(lower, "semibold") || Contains(lower, "demi");
    const bool italic = (font && fz_font_is_italic(ctx, font)) || Contains(lower, "italic") || Contains(lower, "oblique");
    std::string family = name.substr(0, name.find_first_of("-,"));
    for (const char* tail : {"PSMT", "MT", "PS"}) {
        const size_t n = std::strlen(tail);
        if (family.size() > n && family.compare(family.size() - n, n, tail) == 0) {
            family.erase(family.size() - n);
            break;
        }
    }
    style.FontFamily = family.empty() ? "Sans" : family;
    style.Weight = bold ? FontWeight::Bold : FontWeight::Normal;
    style.Slant = italic ? FontSlant::Italic : FontSlant::Normal;
    return style;
}

VectorStorage::BlendMode BlendOf(int blendmode) {
    switch (blendmode & FZ_BLEND_MODEMASK) {
        case FZ_BLEND_MULTIPLY: return VectorStorage::BlendMode::Multiply;
        case FZ_BLEND_SCREEN: return VectorStorage::BlendMode::Screen;
        case FZ_BLEND_OVERLAY: return VectorStorage::BlendMode::Overlay;
        case FZ_BLEND_DARKEN: return VectorStorage::BlendMode::Darken;
        case FZ_BLEND_LIGHTEN: return VectorStorage::BlendMode::Lighten;
        case FZ_BLEND_COLOR_DODGE: return VectorStorage::BlendMode::ColorDodge;
        case FZ_BLEND_COLOR_BURN: return VectorStorage::BlendMode::ColorBurn;
        case FZ_BLEND_HARD_LIGHT: return VectorStorage::BlendMode::HardLight;
        case FZ_BLEND_SOFT_LIGHT: return VectorStorage::BlendMode::SoftLight;
        case FZ_BLEND_DIFFERENCE: return VectorStorage::BlendMode::Difference;
        case FZ_BLEND_EXCLUSION: return VectorStorage::BlendMode::Exclusion;
        default: return VectorStorage::BlendMode::Normal;
    }
}

// A copy keeps no ids: each repeat of a pattern cell is a shape of its own.
void ClearIds(VectorElement& element) {
    element.Id.clear();
    if (auto* group = dynamic_cast<VectorGroup*>(&element))
        for (auto& child : group->Children) if (child) ClearIds(*child);
}

std::vector<uint8_t> BufferBytes(fz_context* ctx, fz_buffer* buffer) {
    unsigned char* data = nullptr;
    const size_t size = buffer ? fz_buffer_storage(ctx, buffer, &data) : 0;
    return data && size ? std::vector<uint8_t>(data, data + size) : std::vector<uint8_t>();
}

// ===== THE BUILDER =====

class PageBuilder {
public:
    PageBuilder(fz_context* ctx, VectorDocument& doc, std::shared_ptr<VectorGroup> root, fz_rect page,
                const std::function<void(const std::string&)>& note)
        : ctx(ctx), doc(doc), note(note) {
        stack.push_back(Entry{std::move(root), page, Entry::Kind::Root});
    }

    // ----- paths -----
    void FillPath(const fz_path* path, int evenOdd, fz_matrix ctm, fz_colorspace* cs, const float* color, float alpha,
                  fz_color_params params) {
        if (Suppressed()) return;
        auto shape = MakePath(path, ctm);
        if (!shape) return;
        shape->Style.Fill = ColorOf(cs, color, alpha, params);
        shape->Style.ShapeFillRule = evenOdd ? VectorStorage::FillRule::EvenOdd : VectorStorage::FillRule::NonZero;
        Add(shape);
        lastFill = shape;
        lastFillPath = path;
        lastFillCtm = ctm;
    }

    void StrokePath(const fz_path* path, const fz_stroke_state* stroke, fz_matrix ctm, fz_colorspace* cs,
                    const float* color, float alpha, fz_color_params params) {
        if (Suppressed()) return;
        StrokeData data = StrokeOf(stroke, ctm, ColorOf(cs, color, alpha, params));
        // PDF's B (fill, then stroke the same path) reaches a device as two
        // calls: one shape with both, as a drawing program would have it.
        if (lastFill && lastFillPath == path && std::memcmp(&lastFillCtm, &ctm, sizeof ctm) == 0 &&
            !lastFill->Style.Stroke && !Current()->Children.empty() && Current()->Children.back() == lastFill) {
            lastFill->Style.Stroke = data;
            lastFill.reset();
            return;
        }
        auto shape = MakePath(path, ctm);
        if (!shape) return;
        shape->Style.Fill = FillData(std::monostate{});
        shape->Style.Stroke = data;
        Add(shape);
    }

    void ClipPath(const fz_path* path, int evenOdd, fz_matrix ctm) {
        auto shape = MakePath(path, ctm);
        const fz_rect box = fz_bound_path(ctx, path, nullptr, ctm);
        if (!shape) { PushGroup(Entry::Kind::Clip, fz_empty_rect); return; }
        auto clip = std::make_shared<VectorClipPath>();
        clip->Data.ClipRule = evenOdd ? VectorStorage::FillRule::EvenOdd : VectorStorage::FillRule::NonZero;
        clip->Data.Elements.push_back(shape);
        PushClip(clip, box);
    }

    void ClipStrokePath(const fz_path* path, const fz_stroke_state* stroke, fz_matrix ctm) {
        // The model clips to filled outlines only; the area a stroke covers
        // is close enough to its bounds for what such a clip is used for.
        NoteOnce("A clip to a stroked line is kept as a clip to its bounds.");
        fz_rect box = fz_bound_path(ctx, path, stroke, ctm);
        auto rect = std::make_shared<VectorPath>();
        rect->MoveTo(box.x0, box.y0);
        rect->LineTo(box.x1, box.y0);
        rect->LineTo(box.x1, box.y1);
        rect->LineTo(box.x0, box.y1);
        rect->ClosePath();
        auto clip = std::make_shared<VectorClipPath>();
        clip->Data.Elements.push_back(rect);
        PushClip(clip, box);
    }

    // ----- text -----
    void FillText(const fz_text* text, fz_matrix ctm, fz_colorspace* cs, const float* color, float alpha,
                  fz_color_params params) {
        if (Suppressed()) return;
        const FillData paint = ColorOf(cs, color, alpha, params);
        for (const fz_text_span* span = text->head; span; span = span->next) TextSpan(span, ctm, paint);
    }

    // Outlined text ("Tr 1"): the glyphs' outlines, stroked. A text object
    // in the model is only ever filled.
    void StrokeText(const fz_text* text, const fz_stroke_state* stroke, fz_matrix ctm, fz_colorspace* cs,
                    const float* color, float alpha, fz_color_params params) {
        if (Suppressed()) return;
        const StrokeData data = StrokeOf(stroke, ctm, ColorOf(cs, color, alpha, params));
        for (const fz_text_span* span = text->head; span; span = span->next) {
            auto outline = GlyphOutlines(span, ctm);
            if (!outline) continue;
            outline->Style.Fill = FillData(std::monostate{});
            outline->Style.Stroke = data;
            Add(outline);
        }
    }

    void ClipText(const fz_text* text, fz_matrix ctm) {
        auto clip = std::make_shared<VectorClipPath>();
        fz_rect box = fz_empty_rect;
        for (const fz_text_span* span = text->head; span; span = span->next) {
            if (auto outline = GlyphOutlines(span, ctm)) {
                clip->Data.Elements.push_back(outline);
                const Rect2Dd b = outline->GetBoundingBox();
                box = fz_union_rect(box, fz_make_rect(b.x, b.y, b.x + b.width, b.y + b.height));
            }
        }
        if (clip->Data.Elements.empty()) { PushGroup(Entry::Kind::Clip, fz_empty_rect); return; }
        PushClip(clip, box);
    }

    // ----- pictures -----
    void FillImage(fz_image* image, fz_matrix ctm, float alpha, fz_color_params params) {
        if (Suppressed()) return;
        std::vector<uint8_t> bytes;
        std::string mime;
        if (!image->mask && !image->imagemask && !image->use_colorkey && !image->use_decode) {
            fz_compressed_buffer* cb = fz_compressed_image_buffer(ctx, image);
            fz_colorspace* cs = image->colorspace;
            const bool screenColours = cs && (fz_colorspace_is_rgb(ctx, cs) || fz_colorspace_is_gray(ctx, cs));
            if (cb && cb->buffer && screenColours && cb->params.type == FZ_IMAGE_JPEG &&
                cb->params.u.jpeg.color_transform != 0) {
                bytes = BufferBytes(ctx, cb->buffer);
                mime = "image/jpeg";
            } else if (cb && cb->buffer && cb->params.type == FZ_IMAGE_PNG) {
                bytes = BufferBytes(ctx, cb->buffer);
                mime = "image/png";
            }
        }
        if (bytes.empty()) {
            bytes = ImageAsPng(image, params, nullptr);
            mime = "image/png";
        }
        if (bytes.empty()) { NoteOnce("A picture the PDF holds could not be read."); return; }
        AddImage(std::move(bytes), mime, ctm, alpha);
    }

    // A stencil: the image says where, the colour what.
    void FillImageMask(fz_image* image, fz_matrix ctm, fz_colorspace* cs, const float* color, float alpha,
                       fz_color_params params) {
        if (Suppressed()) return;
        float rgb[3] = {0, 0, 0};
        if (cs && color) fz_convert_color(ctx, cs, color, fz_device_rgb(ctx), rgb, nullptr, params);
        auto bytes = ImageAsPng(image, params, rgb);
        if (bytes.empty()) return;
        AddImage(std::move(bytes), "image/png", ctm, alpha);
    }

    void ClipImageMask(fz_image* image, fz_matrix ctm) {
        (void)image;
        NoteOnce("A picture used as a clip is kept as a clip to its frame.");
        const fz_rect box = fz_transform_rect(fz_unit_rect, ctm);
        auto rect = std::make_shared<VectorPath>();
        const fz_point p[4] = {fz_transform_point_xy(0, 0, ctm), fz_transform_point_xy(1, 0, ctm),
                               fz_transform_point_xy(1, 1, ctm), fz_transform_point_xy(0, 1, ctm)};
        rect->MoveTo(p[0].x, p[0].y);
        for (int i = 1; i < 4; ++i) rect->LineTo(p[i].x, p[i].y);
        rect->ClosePath();
        auto clip = std::make_shared<VectorClipPath>();
        clip->Data.Elements.push_back(rect);
        PushClip(clip, box);
    }

    // A gradient or mesh, drawn to a picture of the area it covers here.
    void FillShade(fz_shade* shade, fz_matrix ctm, float alpha, fz_color_params params) {
        if (Suppressed()) return;
        fz_rect region = fz_intersect_rect(fz_bound_shade(ctx, shade, ctm), stack.back().bounds);
        if (fz_is_empty_rect(region) || fz_is_infinite_rect(region)) return;
        float k = kShadeDpi / 72.0f;
        const double area = static_cast<double>(region.x1 - region.x0) * (region.y1 - region.y0) * k * k;
        if (area > kMaxShadePixels) k *= static_cast<float>(std::sqrt(kMaxShadePixels / area));
        const fz_matrix scale = fz_scale(k, k);
        const fz_irect box = fz_round_rect(fz_transform_rect(region, scale));
        if (box.x1 <= box.x0 || box.y1 <= box.y0) return;
        std::vector<uint8_t> png;
        fz_pixmap* pix = nullptr;
        fz_device* draw = nullptr;
        fz_buffer* buf = nullptr;
        fz_var(pix);
        fz_var(draw);
        fz_var(buf);
        fz_try(ctx) {
            pix = fz_new_pixmap_with_bbox(ctx, fz_device_rgb(ctx), box, nullptr, 1);
            fz_clear_pixmap(ctx, pix);
            draw = fz_new_draw_device(ctx, scale, pix);
            fz_fill_shade(ctx, draw, shade, ctm, alpha, params);
            fz_close_device(ctx, draw);
            buf = fz_new_buffer_from_pixmap_as_png(ctx, pix, params);
            png = BufferBytes(ctx, buf);
        }
        fz_always(ctx) {
            fz_drop_buffer(ctx, buf);
            fz_drop_device(ctx, draw);
            fz_drop_pixmap(ctx, pix);
        }
        fz_catch(ctx) { png.clear(); }
        if (png.empty()) return;
        NoteOnce("Gradients and shadings are kept as pictures.");
        auto image = std::make_shared<VectorImage>();
        image->Bounds = Rect2Dd(box.x0 / k, box.y0 / k, (box.x1 - box.x0) / k, (box.y1 - box.y0) / k);
        image->MimeType = "image/png";
        image->Source = "data:image/png;base64," + Base64Encode(png, false);
        Add(image);
    }

    // ----- structure -----
    void PopClip() {
        if (stack.size() > 1) stack.pop_back();
    }

    // What a soft mask is drawn from is not drawing; what it masks is kept
    // unmasked (begin_mask ... end_mask, the content, then pop_clip).
    void BeginMask() {
        NoteOnce("Soft masks are not kept: what they fade is shown unmasked.");
        ++maskDepth;
    }
    void EndMask() {
        if (maskDepth > 0) --maskDepth;
        PushGroup(Entry::Kind::Clip, stack.back().bounds);
    }

    void BeginGroup(int blendmode, float alpha) {
        auto group = PushGroup(Entry::Kind::Group, stack.back().bounds);
        if (group) {
            group->Style.Opacity = alpha;
            group->Style.Blend = BlendOf(blendmode);
        }
    }
    void EndGroup() {
        if (stack.size() > 1) stack.pop_back();
    }

    // The cell arrives once; end_tile repeats it over the area it fills.
    int BeginTile(fz_rect area, fz_rect view, float xstep, float ystep, fz_matrix ctm) {
        Entry entry{std::make_shared<VectorGroup>(), stack.back().bounds, Entry::Kind::Tile};
        entry.tileArea = fz_intersect_rect(area, stack.back().bounds);
        entry.tileView = view;
        entry.xstep = std::fabs(xstep);
        entry.ystep = std::fabs(ystep);
        entry.tileCtm = ctm;
        stack.push_back(std::move(entry));
        return 0;   // not cached: run the cell's content
    }
    void EndTile() {
        if (stack.size() < 2 || stack.back().kind != Entry::Kind::Tile) return;
        Entry tile = std::move(stack.back());
        stack.pop_back();
        if (Suppressed() || tile.group->Children.empty() || fz_is_empty_rect(tile.tileArea)) return;
        // The cells that can reach the area, in the pattern's own space.
        const fz_rect inPattern = fz_transform_rect(tile.tileArea, fz_invert_matrix(tile.tileCtm));
        auto range = [](float lo, float hi, float viewLo, float viewHi, float step, long& first, long& last) {
            if (step <= 0) { first = last = 0; return; }
            first = static_cast<long>(std::floor((lo - viewHi) / step));
            last = static_cast<long>(std::ceil((hi - viewLo) / step));
        };
        long i0, i1, j0, j1;
        range(inPattern.x0, inPattern.x1, tile.tileView.x0, tile.tileView.x1, tile.xstep, i0, i1);
        range(inPattern.y0, inPattern.y1, tile.tileView.y0, tile.tileView.y1, tile.ystep, j0, j1);
        const double copies = static_cast<double>(i1 - i0 + 1) * static_cast<double>(j1 - j0 + 1);
        if (copies > kMaxTileCopies) NoteOnce("A finely repeated pattern was cut short.");

        auto clip = std::make_shared<VectorClipPath>();
        auto rect = std::make_shared<VectorPath>();
        const fz_rect& a = tile.tileArea;
        rect->MoveTo(a.x0, a.y0);
        rect->LineTo(a.x1, a.y0);
        rect->LineTo(a.x1, a.y1);
        rect->LineTo(a.x0, a.y1);
        rect->ClosePath();
        clip->Data.Elements.push_back(rect);
        auto holder = std::make_shared<VectorGroup>();
        holder->Style.ClipPath = Define(clip);
        long placed = 0;
        for (long j = j0; j <= j1 && placed < kMaxTileCopies; ++j) {
            for (long i = i0; i <= i1 && placed < kMaxTileCopies; ++i, ++placed) {
                const fz_point shift = fz_transform_vector(fz_make_point(i * tile.xstep, j * tile.ystep), tile.tileCtm);
                auto cell = std::static_pointer_cast<VectorGroup>(tile.group->Clone());
                ClearIds(*cell);
                if (shift.x != 0 || shift.y != 0) cell->Transform = Matrix3x3::Translate(shift.x, shift.y);
                holder->AddChild(cell);
            }
        }
        Add(holder);
    }

private:
    struct Entry {
        enum class Kind { Root, Clip, Group, Tile };
        std::shared_ptr<VectorGroup> group;
        fz_rect bounds;   // what can still show inside it, in page space
        Kind kind;
        fz_rect tileArea = fz_empty_rect, tileView = fz_empty_rect;
        float xstep = 0, ystep = 0;
        fz_matrix tileCtm = fz_identity;
    };

    bool Suppressed() const { return maskDepth > 0; }
    VectorGroup* Current() const { return stack.back().group.get(); }

    void Add(const std::shared_ptr<VectorElement>& element) {
        if (!element || Suppressed()) return;
        element->Id = "pdf" + std::to_string(++nextId);
        stack.back().group->AddChild(element);
    }

    std::string Define(const std::shared_ptr<VectorClipPath>& clip) {
        const std::string id = "pdfclip" + std::to_string(++nextClip);
        clip->Id = id;
        doc.AddDefinition(id, clip);
        return id;
    }

    // A group inside the current one (left out of the drawing while a soft
    // mask is being drawn - then it only keeps the nesting).
    std::shared_ptr<VectorGroup> PushGroup(Entry::Kind kind, fz_rect bounds) {
        auto group = std::make_shared<VectorGroup>();
        if (!Suppressed()) {
            group->Id = "pdf" + std::to_string(++nextId);
            stack.back().group->AddChild(group);
        }
        stack.push_back(Entry{group, fz_intersect_rect(bounds, stack.back().bounds), kind});
        return group;
    }

    void PushClip(const std::shared_ptr<VectorClipPath>& clip, fz_rect box) {
        auto group = PushGroup(Entry::Kind::Clip, box);
        if (!Suppressed()) group->Style.ClipPath = Define(clip);
    }

    void NoteOnce(const std::string& text) {
        if (note && noted.insert(text).second) note(text);
    }

    FillData ColorOf(fz_colorspace* cs, const float* color, float alpha, fz_color_params params) {
        float rgb[3] = {0, 0, 0};
        if (cs && color) fz_convert_color(ctx, cs, color, fz_device_rgb(ctx), rgb, nullptr, params);
        return Color(ToByte(rgb[0]), ToByte(rgb[1]), ToByte(rgb[2]), ToByte(alpha));
    }

    static StrokeLineCap CapOf(fz_linecap cap) {
        switch (cap) {
            case FZ_LINECAP_ROUND: return StrokeLineCap::Round;
            case FZ_LINECAP_SQUARE: return StrokeLineCap::Square;
            default: return StrokeLineCap::Butt;
        }
    }

    StrokeData StrokeOf(const fz_stroke_state* stroke, fz_matrix ctm, const FillData& paint) const {
        StrokeData data;
        data.Fill = paint;
        const float k = fz_matrix_expansion(ctm);
        if (stroke) {
            // A width of 0 is the thinnest line the device can draw.
            data.Width = stroke->linewidth > 0 ? stroke->linewidth * k : 0.25f;
            data.LineCap = CapOf(stroke->start_cap);
            data.LineJoin = stroke->linejoin == FZ_LINEJOIN_ROUND   ? StrokeLineJoin::Round
                            : stroke->linejoin == FZ_LINEJOIN_BEVEL ? StrokeLineJoin::Bevel
                                                                    : StrokeLineJoin::Miter;
            data.MiterLimit = stroke->miterlimit;
            for (int i = 0; i < stroke->dash_len && i < 32; ++i) data.DashArray.push_back(stroke->dash_list[i] * k);
            data.DashOffset = stroke->dash_phase * k;
        }
        return data;
    }

    struct Walk {
        VectorPath* out;
        fz_matrix ctm;
        bool any = false;
    };

    std::shared_ptr<VectorPath> MakePath(const fz_path* path, fz_matrix ctm) {
        static const fz_path_walker walker = {
            [](fz_context*, void* arg, float x, float y) {
                auto* w = static_cast<Walk*>(arg);
                const fz_point p = fz_transform_point_xy(x, y, w->ctm);
                w->out->MoveTo(p.x, p.y);
            },
            [](fz_context*, void* arg, float x, float y) {
                auto* w = static_cast<Walk*>(arg);
                const fz_point p = fz_transform_point_xy(x, y, w->ctm);
                w->out->LineTo(p.x, p.y);
                w->any = true;
            },
            [](fz_context*, void* arg, float x1, float y1, float x2, float y2, float x3, float y3) {
                auto* w = static_cast<Walk*>(arg);
                const fz_point a = fz_transform_point_xy(x1, y1, w->ctm);
                const fz_point b = fz_transform_point_xy(x2, y2, w->ctm);
                const fz_point c = fz_transform_point_xy(x3, y3, w->ctm);
                w->out->CurveTo(a.x, a.y, b.x, b.y, c.x, c.y);
                w->any = true;
            },
            [](fz_context*, void* arg) { static_cast<Walk*>(arg)->out->ClosePath(); },
            nullptr, nullptr, nullptr, nullptr};
        auto shape = std::make_shared<VectorPath>();
        Walk walk{shape.get(), ctm};
        fz_walk_path(ctx, path, &walker, &walk);
        return walk.any ? shape : nullptr;
    }

    // The glyphs of a span as one compound path, in page space.
    std::shared_ptr<VectorPath> GlyphOutlines(const fz_text_span* span, fz_matrix ctm) {
        auto shape = std::make_shared<VectorPath>();
        bool any = false;
        for (int i = 0; i < span->len; ++i) {
            const fz_text_item& item = span->items[i];
            if (item.gid < 0) continue;
            fz_matrix trm = span->trm;
            trm.e = item.x;
            trm.f = item.y;
            fz_path* outline = nullptr;
            fz_try(ctx) { outline = fz_outline_glyph(ctx, span->font, item.gid, fz_concat(trm, ctm)); }
            fz_catch(ctx) { outline = nullptr; }
            if (!outline) continue;
            Walk walk{shape.get(), fz_identity};
            static const fz_path_walker walker = {
                [](fz_context*, void* arg, float x, float y) { static_cast<Walk*>(arg)->out->MoveTo(x, y); },
                [](fz_context*, void* arg, float x, float y) {
                    static_cast<Walk*>(arg)->out->LineTo(x, y);
                    static_cast<Walk*>(arg)->any = true;
                },
                [](fz_context*, void* arg, float x1, float y1, float x2, float y2, float x3, float y3) {
                    static_cast<Walk*>(arg)->out->CurveTo(x1, y1, x2, y2, x3, y3);
                    static_cast<Walk*>(arg)->any = true;
                },
                [](fz_context*, void* arg) { static_cast<Walk*>(arg)->out->ClosePath(); },
                nullptr, nullptr, nullptr, nullptr};
            fz_walk_path(ctx, outline, &walker, &walk);
            fz_drop_path(ctx, outline);
            any = any || walk.any;
        }
        if (!any) {
            NoteOnce("Text in a font without outlines (Type 3) was left out.");
            return nullptr;
        }
        return shape;
    }

    // A span is cut into lines (a VectorText each) and words (a span each,
    // with its own position), so the words stay where the PDF set them
    // whatever font draws them here.
    void TextSpan(const fz_text_span* span, fz_matrix ctm, const FillData& paint) {
        bool hasUnicode = false;
        for (int i = 0; i < span->len && !hasUnicode; ++i) hasUnicode = span->items[i].ucs > 0;
        if (!hasUnicode || span->wmode != 0) {
            // Nothing to type: the shapes, as the page shows them.
            if (!hasUnicode) NoteOnce("Text without character codes is kept as outlines.");
            else NoteOnce("Vertical text is kept as outlines.");
            if (auto outline = GlyphOutlines(span, ctm)) {
                outline->Style.Fill = paint;
                Add(outline);
            }
            return;
        }
        const VectorTextStyle base = FontStyleOf(ctx, span->font);
        std::shared_ptr<VectorText> line;
        TextSpanData word;
        bool wordOpen = false;
        fz_point expected{0, 0};
        double size = 0, angle = 0;
        fz_point direction{1, 0};
        Matrix3x3 toLine = Matrix3x3::Identity();   // page -> the line's upright space

        auto closeWord = [&]() {
            if (wordOpen && !word.Text.empty()) line->AddSpan(word);
            word = TextSpanData();
            wordOpen = false;
        };
        auto closeLine = [&]() {
            closeWord();
            if (line && !line->Spans.empty()) Add(line);
            line.reset();
        };

        for (int i = 0; i < span->len; ++i) {
            const fz_text_item& item = span->items[i];
            if (item.gid < 0) {   // more characters for the glyph before
                if (wordOpen) AppendUtf8(word.Text, item.ucs);
                continue;
            }
            fz_matrix trm = span->trm;
            trm.e = item.x;
            trm.f = item.y;
            const fz_matrix m = fz_concat(trm, ctm);
            const fz_point origin = fz_make_point(m.e, m.f);
            const double itemSize = fz_matrix_expansion(m);
            const double itemAngle = std::atan2(m.b, m.a);
            if (itemSize <= 0) continue;
            const float advance = fz_advance_glyph(ctx, span->font, item.gid, 0);
            const fz_point step = fz_transform_vector(fz_make_point(advance, 0), m);

            bool newLine = !line || std::fabs(itemSize - size) > 0.05 * size || std::fabs(itemAngle - angle) > 0.01;
            double gap = 0;
            if (!newLine) {
                // Along the line and across it, in the line's own terms.
                const double dx = origin.x - expected.x, dy = origin.y - expected.y;
                gap = dx * direction.x + dy * direction.y;
                const double across = -dx * direction.y + dy * direction.x;
                newLine = std::fabs(across) > 0.3 * size || gap < -0.5 * size || gap > 4.0 * size;
            }
            if (newLine) {
                closeLine();
                size = itemSize;
                angle = itemAngle;
                const double len = std::hypot(m.a, m.b);
                direction = len > 0 ? fz_make_point(static_cast<float>(m.a / len), static_cast<float>(m.b / len))
                                    : fz_make_point(1, 0);
                line = std::make_shared<VectorText>();
                line->BaseStyle = base;
                line->BaseStyle.FontSize = static_cast<float>(size);
                line->Style.Fill = paint;
                if (std::fabs(angle) > 0.01) {
                    // Rotated: the line is set upright at its start and turned.
                    line->Transform = Matrix3x3::Translate(origin.x, origin.y) * Matrix3x3::Rotate(angle);
                    toLine = line->Transform->Inverse();
                    line->Position = Point2Dd(0, 0);
                } else {
                    toLine = Matrix3x3::Identity();
                    line->Position = Point2Dd(origin.x, origin.y);
                }
            } else if (gap > 0.2 * size) {
                // A gap the PDF left instead of a space.
                if (wordOpen && !word.Text.empty() && word.Text.back() != ' ') word.Text.push_back(' ');
                closeWord();
            }
            if (item.ucs == ' ' || item.ucs == 0xA0) {
                if (wordOpen) word.Text.push_back(' ');
                closeWord();
            } else {
                if (!wordOpen) {
                    word.Position = toLine.Transform(Point2Dd(origin.x, origin.y));
                    wordOpen = true;
                }
                AppendUtf8(word.Text, item.ucs > 0 ? item.ucs : 0xFFFD);
            }
            expected = fz_make_point(origin.x + step.x, origin.y + step.y);
        }
        closeLine();
    }

    // The image, decoded, as PNG - with its soft mask as alpha, or (a
    // stencil, `stencil` its colour) as that colour where it marks.
    std::vector<uint8_t> ImageAsPng(fz_image* image, fz_color_params params, const float* stencil) {
        std::vector<uint8_t> png;
        fz_pixmap* src = nullptr;
        fz_pixmap* rgb = nullptr;
        fz_pixmap* mask = nullptr;
        fz_pixmap* scaled = nullptr;
        fz_pixmap* out = nullptr;
        fz_buffer* buf = nullptr;
        fz_var(src);
        fz_var(rgb);
        fz_var(mask);
        fz_var(scaled);
        fz_var(out);
        fz_var(buf);
        fz_try(ctx) {
            src = fz_get_pixmap_from_image(ctx, image, nullptr, nullptr, nullptr, nullptr);
            const int w = fz_pixmap_width(ctx, src), h = fz_pixmap_height(ctx, src);
            out = fz_new_pixmap(ctx, fz_device_rgb(ctx), w, h, nullptr, 1);
            unsigned char* o = fz_pixmap_samples(ctx, out);
            const int ostride = fz_pixmap_stride(ctx, out);
            if (stencil) {
                // An image mask's pixmap is its coverage.
                const unsigned char* s = fz_pixmap_samples(ctx, src);
                const int n = fz_pixmap_components(ctx, src), sstride = fz_pixmap_stride(ctx, src);
                const int a = fz_pixmap_alpha(ctx, src);
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < w; ++x) {
                        const unsigned char c = s[y * sstride + x * n + (a ? n - 1 : 0)];
                        unsigned char* p = o + y * ostride + x * 4;
                        // MuPDF's pixmaps carry premultiplied alpha.
                        for (int k = 0; k < 3; ++k) p[k] = static_cast<unsigned char>(ToByte(stencil[k]) * c / 255);
                        p[3] = c;
                    }
            } else {
                fz_pixmap* colour = src;
                if (!fz_pixmap_colorspace(ctx, src) || !fz_colorspace_is_rgb(ctx, fz_pixmap_colorspace(ctx, src))) {
                    rgb = fz_convert_pixmap(ctx, src, fz_device_rgb(ctx), nullptr, nullptr, params, 1);
                    colour = rgb;
                }
                const unsigned char* s = fz_pixmap_samples(ctx, colour);
                const int n = fz_pixmap_components(ctx, colour), sstride = fz_pixmap_stride(ctx, colour);
                const bool hasAlpha = fz_pixmap_alpha(ctx, colour) != 0;
                const unsigned char* m = nullptr;
                int mn = 1, mstride = 0;
                if (image->mask) {
                    mask = fz_get_pixmap_from_image(ctx, image->mask, nullptr, nullptr, nullptr, nullptr);
                    if (fz_pixmap_width(ctx, mask) != w || fz_pixmap_height(ctx, mask) != h) {
                        scaled = fz_scale_pixmap(ctx, mask, 0, 0, static_cast<float>(w), static_cast<float>(h), nullptr);
                        if (scaled) { fz_drop_pixmap(ctx, mask); mask = scaled; scaled = nullptr; }
                    }
                    if (fz_pixmap_width(ctx, mask) == w && fz_pixmap_height(ctx, mask) == h) {
                        m = fz_pixmap_samples(ctx, mask);
                        mn = fz_pixmap_components(ctx, mask);
                        mstride = fz_pixmap_stride(ctx, mask);
                    }
                }
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < w; ++x) {
                        const unsigned char* q = s + y * sstride + x * n;
                        unsigned char* p = o + y * ostride + x * 4;
                        int alpha = hasAlpha ? q[n - 1] : 255;
                        int r = q[0], g = q[1], b = q[2];
                        if (m) {
                            const int ma = m[y * mstride + x * mn];
                            r = r * ma / 255; g = g * ma / 255; b = b * ma / 255;
                            alpha = alpha * ma / 255;
                        }
                        p[0] = static_cast<unsigned char>(r);
                        p[1] = static_cast<unsigned char>(g);
                        p[2] = static_cast<unsigned char>(b);
                        p[3] = static_cast<unsigned char>(alpha);
                    }
            }
            buf = fz_new_buffer_from_pixmap_as_png(ctx, out, params);
            png = BufferBytes(ctx, buf);
        }
        fz_always(ctx) {
            fz_drop_buffer(ctx, buf);
            fz_drop_pixmap(ctx, out);
            fz_drop_pixmap(ctx, scaled);
            fz_drop_pixmap(ctx, mask);
            fz_drop_pixmap(ctx, rgb);
            fz_drop_pixmap(ctx, src);
        }
        fz_catch(ctx) { png.clear(); }
        return png;
    }

    // The unit square the PDF maps a picture onto, as the picture's frame -
    // or, turned or sheared, as a unit picture under that transform.
    void AddImage(std::vector<uint8_t> bytes, const std::string& mime, fz_matrix ctm, float alpha) {
        auto image = std::make_shared<VectorImage>();
        image->MimeType = mime;
        image->Source = "data:" + mime + ";base64," + Base64Encode(bytes, false);
        if (std::fabs(ctm.b) < 1e-6f && std::fabs(ctm.c) < 1e-6f && ctm.a > 0 && ctm.d > 0) {
            image->Bounds = Rect2Dd(ctm.e, ctm.f, ctm.a, ctm.d);
        } else {
            image->Bounds = Rect2Dd(0, 0, 1, 1);
            image->Transform = ToMatrix(ctm);
        }
        if (alpha < 1.0f) image->Style.Opacity = alpha;
        Add(image);
    }

    fz_context* ctx;
    VectorDocument& doc;
    const std::function<void(const std::string&)>& note;
    std::vector<Entry> stack;
    std::set<std::string> noted;
    int maskDepth = 0;
    long nextId = 0;
    long nextClip = 0;
    std::shared_ptr<VectorPath> lastFill;
    const fz_path* lastFillPath = nullptr;
    fz_matrix lastFillCtm = fz_identity;
};

// ===== THE DEVICE =====

struct VectorDevice {
    fz_device super;
    PageBuilder* builder;
};

PageBuilder& B(fz_device* dev) { return *reinterpret_cast<VectorDevice*>(dev)->builder; }

// Two callbacks whose signature MuPDF changed after 1.23: end_mask gained the
// mask's transfer function, begin_tile the document id. The parameters after
// the ones read here are deduced from the slot the function is assigned to,
// so one source builds against the 1.23 of the Linux distributions and the
// newer MuPDF vcpkg gives macOS and Windows.
template <typename... Rest>
void EndMaskCallback(fz_context*, fz_device* d, Rest...) { B(d).EndMask(); }

template <typename... Rest>
int BeginTileCallback(fz_context*, fz_device* d, fz_rect area, fz_rect view, float xs, float ys, fz_matrix m,
                      Rest...) {
    return B(d).BeginTile(area, view, xs, ys, m);
}

fz_device* NewVectorDevice(fz_context* ctx, PageBuilder& builder) {
    auto* dev = fz_new_derived_device(ctx, VectorDevice);
    dev->builder = &builder;
    dev->super.fill_path = [](fz_context*, fz_device* d, const fz_path* p, int eo, fz_matrix m, fz_colorspace* cs,
                              const float* c, float a, fz_color_params cp) { B(d).FillPath(p, eo, m, cs, c, a, cp); };
    dev->super.stroke_path = [](fz_context*, fz_device* d, const fz_path* p, const fz_stroke_state* s, fz_matrix m,
                                fz_colorspace* cs, const float* c, float a, fz_color_params cp) {
        B(d).StrokePath(p, s, m, cs, c, a, cp);
    };
    dev->super.clip_path = [](fz_context*, fz_device* d, const fz_path* p, int eo, fz_matrix m, fz_rect) {
        B(d).ClipPath(p, eo, m);
    };
    dev->super.clip_stroke_path = [](fz_context*, fz_device* d, const fz_path* p, const fz_stroke_state* s,
                                     fz_matrix m, fz_rect) { B(d).ClipStrokePath(p, s, m); };
    dev->super.fill_text = [](fz_context*, fz_device* d, const fz_text* t, fz_matrix m, fz_colorspace* cs,
                              const float* c, float a, fz_color_params cp) { B(d).FillText(t, m, cs, c, a, cp); };
    dev->super.stroke_text = [](fz_context*, fz_device* d, const fz_text* t, const fz_stroke_state* s, fz_matrix m,
                                fz_colorspace* cs, const float* c, float a, fz_color_params cp) {
        B(d).StrokeText(t, s, m, cs, c, a, cp);
    };
    dev->super.clip_text = [](fz_context*, fz_device* d, const fz_text* t, fz_matrix m, fz_rect) {
        B(d).ClipText(t, m);
    };
    dev->super.clip_stroke_text = [](fz_context*, fz_device* d, const fz_text* t, const fz_stroke_state*,
                                     fz_matrix m, fz_rect) { B(d).ClipText(t, m); };
    dev->super.fill_shade = [](fz_context*, fz_device* d, fz_shade* s, fz_matrix m, float a, fz_color_params cp) {
        B(d).FillShade(s, m, a, cp);
    };
    dev->super.fill_image = [](fz_context*, fz_device* d, fz_image* i, fz_matrix m, float a, fz_color_params cp) {
        B(d).FillImage(i, m, a, cp);
    };
    dev->super.fill_image_mask = [](fz_context*, fz_device* d, fz_image* i, fz_matrix m, fz_colorspace* cs,
                                    const float* c, float a, fz_color_params cp) {
        B(d).FillImageMask(i, m, cs, c, a, cp);
    };
    dev->super.clip_image_mask = [](fz_context*, fz_device* d, fz_image* i, fz_matrix m, fz_rect) {
        B(d).ClipImageMask(i, m);
    };
    dev->super.pop_clip = [](fz_context*, fz_device* d) { B(d).PopClip(); };
    dev->super.begin_mask = [](fz_context*, fz_device* d, fz_rect, int, fz_colorspace*, const float*,
                               fz_color_params) { B(d).BeginMask(); };
    dev->super.end_mask = EndMaskCallback;
    dev->super.begin_group = [](fz_context*, fz_device* d, fz_rect, fz_colorspace*, int, int, int blend, float a) {
        B(d).BeginGroup(blend, a);
    };
    dev->super.end_group = [](fz_context*, fz_device* d) { B(d).EndGroup(); };
    dev->super.begin_tile = BeginTileCallback;
    dev->super.end_tile = [](fz_context*, fz_device* d) { B(d).EndTile(); };
    return &dev->super;
}

std::string Metadata(fz_context* ctx, fz_document* doc, const char* key) {
    char value[512] = {0};
    if (fz_lookup_metadata(ctx, doc, key, value, sizeof value) <= 0) return std::string();
    return value;
}

}   // namespace

std::shared_ptr<VectorStorage::VectorDocument> ImportPDFAsVectorDocument(
        const std::vector<uint8_t>& data, const std::function<void(const std::string&)>& note,
        int pageNumber, const std::string& password) {
    auto say = [&note](const std::string& s) { if (note) note(s); };
    if (data.empty()) { say("The file is empty."); return nullptr; }
    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) { say("The PDF reader could not start."); return nullptr; }
    std::shared_ptr<VectorDocument> result;
    fz_stream* stream = nullptr;
    fz_document* document = nullptr;
    fz_page* page = nullptr;
    fz_device* dev = nullptr;
    fz_var(stream);
    fz_var(document);
    fz_var(page);
    fz_var(dev);
    fz_try(ctx) {
        fz_register_document_handlers(ctx);
        stream = fz_open_memory(ctx, data.data(), data.size());
        document = fz_open_document_with_stream(ctx, "application/pdf", stream);
        if (fz_needs_password(ctx, document) && !fz_authenticate_password(ctx, document, password.c_str()))
            fz_throw(ctx, FZ_ERROR_GENERIC, password.empty() ? "The PDF is password-protected."
                                                             : "The password is not correct.");
        const int pages = fz_count_pages(ctx, document);
        if (pages <= 0) fz_throw(ctx, FZ_ERROR_GENERIC, "The PDF has no pages.");
        const int wanted = std::clamp(pageNumber, 1, pages);
        page = fz_load_page(ctx, document, wanted - 1);

        // The page as a viewer shows it: the crop box, turned and scaled.
        const fz_rect bounds = fz_bound_page(ctx, page);
        const float w = bounds.x1 - bounds.x0, h = bounds.y1 - bounds.y0;
        if (!(w > 0) || !(h > 0)) fz_throw(ctx, FZ_ERROR_GENERIC, "The page has no size.");

        auto doc = std::make_shared<VectorDocument>();
        doc->Size = Size2Dd{w, h};
        doc->ViewBox = Rect2Dd{0, 0, w, h};
        doc->Title = Metadata(ctx, document, FZ_META_INFO_TITLE);
        doc->Author = Metadata(ctx, document, FZ_META_INFO_AUTHOR);
        auto layer = doc->AddLayer(pages > 1 ? "Page " + std::to_string(wanted) : "Layer 1");

        PageBuilder builder(ctx, *doc, layer, fz_make_rect(0, 0, w, h), note);
        dev = NewVectorDevice(ctx, builder);
        fz_run_page(ctx, page, dev, fz_translate(-bounds.x0, -bounds.y0), nullptr);
        fz_close_device(ctx, dev);
        if (pages > 1)
            say("The PDF has " + std::to_string(pages) + " pages; page " + std::to_string(wanted) + " was opened.");
        result = doc;
    }
    fz_always(ctx) {
        fz_drop_device(ctx, dev);
        fz_drop_page(ctx, page);
        fz_drop_document(ctx, document);
        fz_drop_stream(ctx, stream);
    }
    fz_catch(ctx) {
        say(fz_caught_message(ctx));
        result.reset();
    }
    fz_drop_context(ctx);
    return result;
}

}   // namespace UltraCanvas

#endif   // ULTRACANVAS_PDF_MUPDF
