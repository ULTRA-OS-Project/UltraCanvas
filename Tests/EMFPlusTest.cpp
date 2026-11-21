// Tests/EMFPlusTest.cpp
// EMF+ in the EMF reader, and the EMF a WMF carries.
//
// A dual EMF is built here record by record, as GDI+ writes one: each EMF+
// drawing is followed by a GDI copy for players without EMF+, drawn
// opaque and somewhere else. Checked:
//   - a translucent EMF+ fill is read with its alpha, and its GDI copy is
//     not drawn as well;
//   - GDI drawing after an EMF+ GetDC is drawn;
//   - an EMF+ record the reader cannot draw (an image) leaves its GDI copy
//     to draw it, and is reported;
//   - an EMF+-only file keeps what it can draw;
//   - a WMF carrying that EMF in META_ESCAPE_ENHANCED_METAFILE records is
//     read from it.
//
// Usage: EMFPlusTest
// Exit code is the number of failed checks.
// Version: 1.0.0
// Last Modified: 2026-11-20
// Author: UltraCanvas Framework

#include "UltraCanvasVectorFormatsPlugin.h"
#include "UltraCanvasMetafileConverters.h"
#include "UltraCanvasVectorRaster.h"
#include "UltraCanvasRasterLayer.h"
#include "UltraCanvasImage.h"
#include "DataFormats/UltraCanvasVectorStorage.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <variant>
#include <vector>

using namespace UltraCanvas;
using namespace UltraCanvas::VectorStorage;
using namespace UltraCanvas::VectorConverter;

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++failures;
}

// ----- byte building -----

struct Bytes {
    std::string s;
    void U8(uint8_t v) { s.push_back(static_cast<char>(v)); }
    void U16(uint16_t v) { U8(v & 0xFF); U8(v >> 8); }
    void U32(uint32_t v) { U16(v & 0xFFFF); U16(v >> 16); }
    void I32(int32_t v) { U32(static_cast<uint32_t>(v)); }
    void F32(float f) { uint32_t u; std::memcpy(&u, &f, 4); U32(u); }
};

// One EMF record: type, size, body.
void Emr(Bytes& out, uint32_t type, const std::string& body) {
    out.U32(type);
    out.U32(static_cast<uint32_t>(8 + body.size()));
    out.s += body;
}

// An EMR_COMMENT carrying EMF+ records.
void EmfPlus(Bytes& out, const std::string& records) {
    Bytes body;
    body.U32(static_cast<uint32_t>(4 + records.size()));   // DataSize
    body.s += "EMF+";
    body.s += records;
    Emr(out, 70, body.s);
}

std::string PlusRecord(uint16_t type, uint16_t flags, const std::string& data) {
    Bytes r;
    r.U16(type);
    r.U16(flags);
    r.U32(static_cast<uint32_t>(12 + data.size()));
    r.U32(static_cast<uint32_t>(data.size()));
    r.s += data;
    return r.s;
}

std::string PlusHeader(bool dual) {
    Bytes d;
    d.U32(0xDBC01002);   // version
    d.U32(1);            // reference device is a video display
    d.U32(96);
    d.U32(96);
    return PlusRecord(0x4001, dual ? 1 : 0, d.s);
}

// FillPolygon with an inline ARGB colour (S) and 16-bit points (C).
std::string PlusFillPolygon(uint32_t argb, const std::vector<std::pair<int, int>>& pts) {
    Bytes d;
    d.U32(argb);
    d.U32(static_cast<uint32_t>(pts.size()));
    for (auto [x, y] : pts) { d.U16(static_cast<uint16_t>(x)); d.U16(static_cast<uint16_t>(y)); }
    return PlusRecord(0x400C, 0x8000 | 0x4000, d.s);
}

std::string PlusFillEllipse(uint32_t argb, float x, float y, float w, float h) {
    Bytes d;
    d.U32(argb);
    d.F32(x); d.F32(y); d.F32(w); d.F32(h);
    return PlusRecord(0x400E, 0x8000, d.s);
}

std::string Header(int widthPx, int heightPx) {
    Bytes h;
    h.I32(0); h.I32(0); h.I32(widthPx - 1); h.I32(heightPx - 1);   // bounds (device pixels)
    // Frame in 0.01 mm at 96 dpi: 2540 / 96 per pixel.
    h.I32(0); h.I32(0); h.I32(widthPx * 2540 / 96); h.I32(heightPx * 2540 / 96);
    h.U32(0x464D4520);   // " EMF"
    h.U32(0x10000);      // version
    h.U32(0);            // bytes (unused by the reader)
    h.U32(0);            // records
    h.U16(1); h.U16(0);  // handles, reserved
    h.U32(0); h.U32(0); h.U32(0);   // description, palette entries
    h.I32(1920); h.I32(1080);       // device, pixels
    h.I32(508); h.I32(286);         // device, millimetres
    Bytes out;
    Emr(out, 1, h.s);
    return out.s;
}

std::string Rect(int l, int t, int r, int b) {
    Bytes d;
    d.I32(l); d.I32(t); d.I32(r); d.I32(b);
    return d.s;
}

std::string Polygon16(const std::vector<std::pair<int, int>>& pts) {
    Bytes d;
    d.s += Rect(0, 0, 0, 0);
    d.U32(static_cast<uint32_t>(pts.size()));
    for (auto [x, y] : pts) { d.U16(static_cast<uint16_t>(x)); d.U16(static_cast<uint16_t>(y)); }
    return d.s;
}

std::string Eof() {
    Bytes d;
    d.U32(0); d.U32(16); d.U32(20);
    Bytes out;
    Emr(out, 14, d.s);
    return out.s;
}

// 200 x 100 px (150 x 75 pt). EMF+: a red square at half alpha, its GDI copy
// opaque and 10 px off. After a GetDC, a GDI rectangle. Then an EMF+ image,
// which the reader cannot draw, and its GDI copy, an ellipse.
std::string DualEmf() {
    Bytes out;
    out.s += Header(200, 100);
    EmfPlus(out, PlusHeader(true) + PlusRecord(0x4004, 0, ""));
    // Stock objects: a black brush, so a GDI copy that is drawn shows.
    Bytes black;
    black.U32(0x80000004);
    Emr(out, 37, black.s);
    EmfPlus(out, PlusFillPolygon(0x80FF0000, {{10, 10}, {60, 10}, {60, 60}, {10, 60}}));
    Emr(out, 86, Polygon16({{20, 20}, {70, 20}, {70, 70}, {20, 70}}));   // the GDI copy
    EmfPlus(out, PlusRecord(0x4004, 0, ""));                             // GetDC
    Emr(out, 43, Rect(100, 10, 150, 40));                                // real GDI drawing
    Bytes image;
    image.U32(0); image.U32(0);   // image id, source unit...
    EmfPlus(out, PlusRecord(0x401A, 0, image.s + std::string(32, '\0')));
    Emr(out, 42, Rect(100, 60, 150, 90));                                // its GDI copy
    EmfPlus(out, PlusRecord(0x4004, 0, ""));
    EmfPlus(out, PlusRecord(0x4002, 0, ""));
    out.s += Eof();
    return out.s;
}

std::string PlusOnlyEmf() {
    Bytes out;
    out.s += Header(200, 100);
    EmfPlus(out, PlusHeader(false) + PlusFillEllipse(0xFF0000FF, 20, 20, 40, 40) +
                 PlusRecord(0x401A, 0, std::string(40, '\0')) + PlusRecord(0x4002, 0, ""));
    out.s += Eof();
    return out.s;
}

// The EMF inside a WMF, in pieces, as GDI+ writes it, and a WMF polygon as
// the copy for players that cannot read it.
std::string WmfCarrying(const std::string& emf) {
    Bytes w;
    // Placeable header: key, handle, bounds, units per inch, reserved, checksum.
    w.U32(0x9AC6CDD7); w.U16(0);
    w.U16(0); w.U16(0); w.U16(200); w.U16(100);
    w.U16(96); w.U32(0); w.U16(0);
    // Standard header (9 words).
    w.U16(1); w.U16(9); w.U16(0x0300); w.U32(0); w.U16(0); w.U32(0); w.U16(0);
    const size_t pieceSize = 100;
    const uint32_t count = static_cast<uint32_t>((emf.size() + pieceSize - 1) / pieceSize);
    for (size_t at = 0; at < emf.size(); at += pieceSize) {
        const std::string piece = emf.substr(at, pieceSize);
        Bytes p;
        p.U16(0x000F);                                       // MFCOMMENT
        p.U16(static_cast<uint16_t>(34 + piece.size()));     // byte count
        p.U32(0x43464D57);                                   // "WMFC"
        p.U32(1);                                            // enhanced metafile
        p.U32(0x10000);
        p.U16(0);                                            // checksum
        p.U32(0);                                            // flags
        p.U32(count);
        p.U32(static_cast<uint32_t>(piece.size()));
        p.U32(static_cast<uint32_t>(emf.size() - at - piece.size()));
        p.U32(static_cast<uint32_t>(emf.size()));
        p.s += piece;
        if (p.s.size() % 2) p.U8(0);
        w.U32(static_cast<uint32_t>(3 + p.s.size() / 2));
        w.U16(0x0626);
        w.s += p.s;
    }
    // META_POLYGON, the opaque copy: 4 points.
    w.U32(3 + 1 + 8); w.U16(0x0324); w.U16(4);
    for (int v : {20, 20, 70, 20, 70, 70, 20, 70}) w.U16(static_cast<uint16_t>(v));
    w.U32(3); w.U16(0);   // META_EOF
    return w.s;
}

void Count(const VectorDocument& doc, int& paths, int& polygons, int& rects, int& ellipses,
           std::shared_ptr<VectorPath>& firstPath) {
    for (const auto& l : doc.Layers)
        for (const auto& c : l->Children) {
            if (c->Type == VectorElementType::Path) {
                ++paths;
                if (!firstPath) firstPath = std::static_pointer_cast<VectorPath>(c);
            }
            if (c->Type == VectorElementType::Polygon) ++polygons;
            if (c->Type == VectorElementType::Rectangle) ++rects;
            if (c->Type == VectorElementType::Ellipse) ++ellipses;
        }
}

std::shared_ptr<UCRasterLayer> Draw(const VectorDocument& doc, int width) {
    VectorRasterOptions o;
    o.width = width;
    o.background = RasterPixel(255, 255, 255, 255);
    std::string error;
    return RasterizeVectorDocument(doc, o, error);
}

bool HalfRed(const RasterPixel& p) {   // red at half alpha over white
    return p.r > 230 && p.g > 100 && p.g < 160 && p.b > 100 && p.b < 160;
}

void TestDual() {
    std::printf("--- a dual EMF ---\n");
    std::vector<std::string> notes;
    ConversionOptions options;
    options.WarningCallback = [&](const std::string& n) { notes.push_back(n); };
    auto doc = EMFConverter().ImportFromString(DualEmf(), options);
    Check(doc != nullptr, "read");
    if (!doc) return;
    int paths = 0, polygons = 0, rects = 0, ellipses = 0;
    std::shared_ptr<VectorPath> square;
    Count(*doc, paths, polygons, rects, ellipses, square);
    Check(paths == 1 && polygons == 0, "the EMF+ square is read, its GDI copy is not (paths " +
                                            std::to_string(paths) + ", GDI polygons " + std::to_string(polygons) + ")");
    bool alpha = false;
    if (square && square->Style.Fill)
        if (const Color* c = std::get_if<Color>(&*square->Style.Fill)) alpha = c->r == 255 && c->a == 128;
    Check(alpha, "its fill keeps its alpha (128)");
    Check(rects == 1, "GDI drawing after GetDC is drawn");
    Check(ellipses == 1, "an EMF+ image the reader cannot draw leaves its GDI copy to draw it");
    bool reported = false;
    for (const auto& n : notes) reported = reported || n.find("0x401a") != std::string::npos;
    Check(reported, "and is reported");

    if (auto layer = Draw(*doc, 400)) {   // 2 pixels a device pixel
        Check(HalfRed(layer->GetPixel(30, 30)), "drawn: the square is translucent where the EMF+ puts it");
        const RasterPixel off = layer->GetPixel(130, 130);   // inside the GDI copy only
        Check(off.r > 240 && off.g > 240 && off.b > 240, "and nothing is drawn where its GDI copy was");
    } else {
        Check(false, "drawn");
    }
}

void TestPlusOnly() {
    std::printf("--- an EMF+-only EMF ---\n");
    auto doc = EMFConverter().ImportFromString(PlusOnlyEmf(), ConversionOptions());
    int paths = 0, polygons = 0, rects = 0, ellipses = 0;
    std::shared_ptr<VectorPath> circle;
    if (doc) Count(*doc, paths, polygons, rects, ellipses, circle);
    Check(paths == 1, "with no GDI copy to fall back on, what can be drawn is kept");
}

void TestWmf() {
    std::printf("--- a WMF carrying its EMF ---\n");
    auto doc = WMFConverter().ImportFromString(WmfCarrying(DualEmf()), ConversionOptions());
    int paths = 0, polygons = 0, rects = 0, ellipses = 0;
    std::shared_ptr<VectorPath> square;
    if (doc) Count(*doc, paths, polygons, rects, ellipses, square);
    bool alpha = false;
    if (square && square->Style.Fill)
        if (const Color* c = std::get_if<Color>(&*square->Style.Fill)) alpha = c->a == 128;
    Check(doc && paths == 1 && polygons == 0 && alpha, "read from the EMF: the translucent square, not the WMF copy");
}

}   // namespace

int main() {
    UCImage::InitializeImageSubsysterm("EMFPlusTest");
    RegisterVectorFormatsPlugin();
    TestDual();
    TestPlusOnly();
    TestWmf();
    std::printf("%d failure(s)\n", failures);
    return failures;
}
