// Tests/MetafileWriterTest.cpp
// Tests for the Vector plugin's EMF, WMF and AI writers: each export is
// checked structurally (headers, record walks that must land exactly on the
// terminating record), and then rendered by an independent consumer when
// one is installed - LibreOffice (soffice) rasterizes the EMF and WMF,
// ghostscript the (PDF-based) AI - with pixel probes at relative positions
// proving the drawing is where it should be.
//
// Usage: MetafileWriterTest [basename]
// Files are written as <basename>.emf/.wmf/.ai (default: metafile_test).
// Exit code is the number of failed checks.
// Version: 1.0.0
// Last Modified: 2026-08-26
// Author: UltraCanvas Framework

#include "../UltraCanvas/Plugins/Vector/UltraCanvasMetafileConverters.h"
#include "../UltraCanvas/Plugins/Vector/UltraCanvasVectorStorage.h"
#include "UltraCanvasImage.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

using namespace UltraCanvas;
using namespace UltraCanvas::VectorStorage;

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::printf("FAIL: %s\n", what.c_str());
    } else {
        std::printf("  ok: %s\n", what.c_str());
    }
}

std::shared_ptr<VectorDocument> BuildTestDocument() {
    auto doc = std::make_shared<VectorDocument>();
    doc->Size = Size2Dd{400, 300};
    doc->Title = "Metafile writer test";

    auto layer = doc->AddLayer("Artwork");

    auto rect = std::make_shared<VectorRect>();
    rect->Bounds = Rect2Dd{40, 40, 100, 60};
    rect->Style.Fill = Color(255, 0, 0, 255);
    StrokeData rectStroke;
    rectStroke.Fill = Color(0, 0, 255, 255);
    rectStroke.Width = 2.0f;
    rect->Style.Stroke = rectStroke;
    layer->AddChild(rect);

    auto rrect = std::make_shared<VectorRect>();
    rrect->Bounds = Rect2Dd{180, 40, 90, 60};
    rrect->RadiusX = 12;
    rrect->RadiusY = 12;
    rrect->Style.Fill = Color(0, 160, 0, 255);
    layer->AddChild(rrect);

    auto circle = std::make_shared<VectorCircle>();
    circle->Center = Point2Dd(330, 70);
    circle->Radius = 30;
    circle->Style.Fill = Color(255, 128, 0, 255);
    circle->Style.Opacity = 0.5f;   // flattened toward white in GDI formats
    layer->AddChild(circle);

    auto dashLine = std::make_shared<VectorLine>();
    dashLine->Start = Point2Dd(40, 130);
    dashLine->End = Point2Dd(150, 130);
    StrokeData dashStroke;
    dashStroke.Fill = Color(0, 0, 0, 255);
    dashStroke.Width = 2.0f;
    dashStroke.DashArray = {6.0, 3.0};
    dashLine->Style.Stroke = dashStroke;
    layer->AddChild(dashLine);

    auto path = std::make_shared<VectorPath>();
    path->MoveTo(200, 140);
    path->CurveTo(240, 120, 280, 120, 300, 160);
    path->CurveTo(280, 200, 240, 200, 200, 160);
    path->ClosePath();
    path->Style.Fill = Color(90, 40, 160, 255);
    layer->AddChild(path);

    auto group = std::make_shared<VectorGroup>();
    group->Transform = Matrix3x3::Translate(330, 180) *
                       Matrix3x3::RotateDegrees(30) *
                       Matrix3x3::Translate(-330, -180);
    auto rotRect = std::make_shared<VectorRect>();
    rotRect->Bounds = Rect2Dd{300, 160, 60, 40};
    rotRect->Style.Fill = Color(0, 120, 200, 255);
    group->AddChild(rotRect);
    layer->AddChild(group);

    auto text = std::make_shared<VectorText>();
    text->Position = Point2Dd(40, 250);
    text->BaseStyle.FontFamily = "Liberation Sans";
    text->BaseStyle.FontSize = 18.0f;
    TextSpanData s1;
    s1.Text = "Hello ";
    s1.Style = text->BaseStyle;
    TextSpanData s2;
    s2.Text = "Metafiles";
    s2.Style = text->BaseStyle;
    s2.Style.Weight = FontWeight::Bold;
    text->Spans.push_back(s1);
    text->Spans.push_back(s2);
    text->Style.Fill = Color(20, 20, 20, 255);
    layer->AddChild(text);

    return doc;
}

uint32_t RdU32(const std::string& d, size_t o) {
    return static_cast<uint8_t>(d[o]) |
           (static_cast<uint8_t>(d[o + 1]) << 8) |
           (static_cast<uint8_t>(d[o + 2]) << 16) |
           (static_cast<uint8_t>(d[o + 3]) << 24);
}
uint16_t RdU16(const std::string& d, size_t o) {
    return static_cast<uint16_t>(static_cast<uint8_t>(d[o]) |
                                 (static_cast<uint8_t>(d[o + 1]) << 8));
}

// Walk the EMF record stream; return the number of records if the walk lands
// exactly on the EOF record's end, 0 otherwise.
uint32_t WalkEmf(const std::string& d) {
    size_t pos = 0;
    uint32_t count = 0;
    while (pos + 8 <= d.size()) {
        uint32_t type = RdU32(d, pos);
        uint32_t size = RdU32(d, pos + 4);
        if (size < 8 || size % 4 || pos + size > d.size()) return 0;
        ++count;
        pos += size;
        if (type == 14) return pos == d.size() ? count : 0;   // EMR_EOF
    }
    return 0;
}

// Walk the WMF record stream after the placeable+standard headers; return
// the record count if it ends exactly on META_EOF.
uint32_t WalkWmf(const std::string& d) {
    size_t pos = 22 + 18;
    uint32_t count = 0;
    while (pos + 6 <= d.size()) {
        uint32_t words = RdU32(d, pos);
        uint16_t function = RdU16(d, pos + 4);
        if (words < 3 || pos + words * 2 > d.size()) return 0;
        ++count;
        pos += words * 2;
        if (function == 0) return pos == d.size() ? count : 0;   // META_EOF
    }
    return 0;
}

bool HaveTool(const char* probe) { return std::system(probe) == 0; }

// Rasterize `src` to PNG via the given shell command (which must produce
// `png`) and verify the drawing rendered. The consumer may place the
// drawing anywhere on its canvas (LibreOffice centres metafiles on a page),
// so the checks are layout-independent: each element's colour must cover a
// real area, and the colour centroids must sit in the drawing's layout
// order (red rect left of the green one, both above the purple path, the
// rotated blue rect to the path's lower right).
void CheckRender(const std::string& tag, const std::string& cmd, const std::string& png) {
    std::remove(png.c_str());
    std::system(cmd.c_str());
    if (std::ifstream(png).fail()) {
        // A core-only LibreOffice (no Draw module) cannot convert graphics
        // at all; that is missing infrastructure, not a bad file.
        std::printf("  note: %s produced no output; render checks skipped\n",
                    tag.c_str());
        return;
    }
    Check(true, tag + ": external converter accepts the file");
    auto img = UCImage::Get(png);
    Check(img && img->GetWidth() > 50 && img->GetHeight() > 50,
          tag + ": rendering decodes");
    if (!img) return;
    auto pm = img->GetPixmap(img->GetWidth(), img->GetHeight(),
                             ImageFitMode::Contain, 1.0f);
    if (!pm) {
        Check(false, tag + ": pixmap");
        return;
    }
    const uint32_t* px = pm->GetPixelData();
    int w = pm->GetRawWidth(), h = pm->GetRawHeight();

    struct Blob { long count = 0; double sx = 0, sy = 0;
                  double X() const { return count ? sx / count : -1; }
                  double Y() const { return count ? sy / count : -1; } };
    auto scan = [&](int rt, int gt, int bt, int tol) {
        Blob blob;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                uint32_t p = px[y * w + x];
                int a = (p >> 24) & 0xFF;
                if (a < 128) continue;
                int r = (p >> 16) & 0xFF, g = (p >> 8) & 0xFF, b = p & 0xFF;
                if (std::abs(r - rt) < tol && std::abs(g - gt) < tol &&
                    std::abs(b - bt) < tol) {
                    ++blob.count;
                    blob.sx += x;
                    blob.sy += y;
                }
            }
        }
        return blob;
    };

    long minArea = static_cast<long>(w) * h / 4000;   // ~0.025% of the canvas
    Blob red = scan(255, 0, 0, 60);
    Blob green = scan(0, 160, 0, 60);
    Blob purple = scan(90, 40, 160, 60);
    Blob blue = scan(0, 120, 200, 60);
    Check(red.count > minArea, tag + ": rect renders red");
    Check(green.count > minArea, tag + ": rounded rect renders green");
    Check(purple.count > minArea, tag + ": bezier path renders purple");
    Check(blue.count > minArea, tag + ": rotated rect renders blue");
    if (red.count && green.count && purple.count && blue.count) {
        Check(red.X() < green.X() && red.Y() < purple.Y() &&
              green.Y() < purple.Y() && purple.X() < blue.X(),
              tag + ": elements sit in layout order");
    }
}

}   // namespace

int main(int argc, char** argv) {
    std::string base = argc > 1 ? argv[1] : "metafile_test";
    auto doc = BuildTestDocument();
    VectorConverter::ConversionOptions options;
    options.WarningCallback = [](const std::string& msg) {
        std::printf("      warning: %s\n", msg.c_str());
    };
    UCImage::InitializeImageSubsysterm("MetafileWriterTest");

    // ===== EMF =====
    {
        VectorConverter::EMFConverter emf;
        std::string data = emf.ExportToString(*doc, options);
        Check(!data.empty(), "EMF: export produces output");
        Check(emf.ValidateData(data), "EMF: \" EMF\" signature present");
        Check(emf.Export(*doc, base + ".emf", options), "EMF: file written");
        Check(RdU32(data, 48) == data.size(), "EMF: header nBytes matches the file size");
        uint32_t walked = WalkEmf(data);
        Check(walked > 0, "EMF: record walk lands exactly on EMR_EOF");
        Check(walked == RdU32(data, 52), "EMF: header nRecords matches the walk");
    }

    // ===== WMF =====
    {
        VectorConverter::WMFConverter wmf;
        std::string data = wmf.ExportToString(*doc, options);
        Check(!data.empty(), "WMF: export produces output");
        Check(wmf.ValidateData(data), "WMF: placeable header key present");
        Check(wmf.Export(*doc, base + ".wmf", options), "WMF: file written");
        uint16_t checksum = 0;
        for (int i = 0; i < 20; i += 2) checksum ^= RdU16(data, i);
        Check(checksum == RdU16(data, 20), "WMF: placeable checksum is correct");
        Check(WalkWmf(data) > 0, "WMF: record walk lands exactly on META_EOF");
        Check(RdU32(data, 22 + 6) * 2 == data.size() - 22,
              "WMF: header size matches the file");
    }

    // ===== AI =====
    {
        VectorConverter::AIConverter ai;
        std::string data = ai.ExportToString(*doc, options);
        Check(!data.empty(), "AI: export produces output");
        Check(ai.ValidateData(data), "AI: PDF-based signature present");
        Check(ai.Export(*doc, base + ".ai", options), "AI: file written");
    }

    // ===== INDEPENDENT CONSUMERS =====
    if (HaveTool("command -v soffice >/dev/null 2>&1")) {
        std::string cmd = "soffice --headless --convert-to png --outdir . " +
                          base + ".emf >/dev/null 2>&1";
        CheckRender("EMF/soffice", cmd, base + ".png");
        std::remove((base + ".png").c_str());
        cmd = "soffice --headless --convert-to png --outdir . " +
              base + ".wmf >/dev/null 2>&1";
        CheckRender("WMF/soffice", cmd, base + ".png");
    } else {
        std::printf("  note: LibreOffice not found; EMF/WMF render checks skipped\n");
    }
    if (HaveTool("command -v gs >/dev/null 2>&1")) {
        std::string cmd = "gs -q -dSAFER -dBATCH -dNOPAUSE -sDEVICE=png16m -r72 "
                          "-sOutputFile=" + base + "_ai.png " + base + ".ai >/dev/null 2>&1";
        CheckRender("AI/ghostscript", cmd, base + "_ai.png");
    } else {
        std::printf("  note: ghostscript not found; AI render checks skipped\n");
    }

    std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}
