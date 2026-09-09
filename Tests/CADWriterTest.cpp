// Tests/CADWriterTest.cpp
// Tests for the Vector plugin's DXF and DWG writers. The DXF export is
// checked structurally and then validated by independent consumers when
// available: ezdxf (the reference Python DXF library - strict read, audit,
// entity/layer queries) and LibreOffice (rasterization with layout-order
// colour checks). The DWG path runs when LibreDWG's dxf2dwg is available
// (ULTRACANVAS_DXF2DWG or PATH): the written DWG must carry the version
// magic, and dwgread must accept it.
//
// Usage: CADWriterTest [basename]
// Exit code is the number of failed checks.
// Version: 1.0.0
// Last Modified: 2026-08-26
// Author: UltraCanvas Framework

#include "../UltraCanvas/Plugins/Vector/UltraCanvasCADConverters.h"
#include "../UltraCanvas/Plugins/Vector/UltraCanvasVectorStorage.h"
#include "UltraCanvasImage.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
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
    StrokeData pathStroke;
    pathStroke.Fill = Color(30, 30, 30, 255);
    pathStroke.Width = 1.5f;
    path->Style.Stroke = pathStroke;
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
    text->SetText("Hello CAD");
    text->Style.Fill = Color(20, 20, 20, 255);
    layer->AddChild(text);

    return doc;
}

bool HaveTool(const char* probe) { return std::system(probe) == 0; }

// Layout-independent colour blob checks, same idea as MetafileWriterTest.
void CheckRaster(const std::string& tag, const std::string& png) {
    auto img = UCImage::Get(png);
    Check(img && img->GetWidth() > 50, tag + ": rendering decodes");
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
                  double X() const { return count ? sx / count : -1; } };
    auto scan = [&](int rt, int gt, int bt, int tol) {
        Blob blob;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                uint32_t p = px[y * w + x];
                if (((p >> 24) & 0xFF) < 128) continue;
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
    // LibreOffice's DXF filter is a legacy one: it takes the ACI fallback
    // colour rather than the 420 true colour and approximates spline hatch
    // edges, so the checks accept the ACI-rounded colours. ezdxf validates
    // the exact values above.
    long minArea = static_cast<long>(w) * h / 4000;
    Blob red = scan(255, 0, 0, 60);
    Blob green = scan(0, 208, 0, 110);
    Check(red.count > minArea, tag + ": rect fills red");
    Check(green.count > minArea, tag + ": rounded rect fills green");
    if (red.count && green.count) {
        Check(red.X() < green.X(), tag + ": elements sit in layout order");
    }
}

}   // namespace

int main(int argc, char** argv) {
    std::string base = argc > 1 ? argv[1] : "cad_writer_test";
    auto doc = BuildTestDocument();
    VectorConverter::ConversionOptions options;
    options.WarningCallback = [](const std::string& msg) {
        std::printf("      warning: %s\n", msg.c_str());
    };
    UCImage::InitializeImageSubsysterm("CADWriterTest");

    std::string dxfPath = base + ".dxf";
    VectorConverter::DXFConverter dxf;
    std::string data = dxf.ExportToString(*doc, options);
    Check(!data.empty(), "DXF: export produces output");
    Check(dxf.Export(*doc, dxfPath, options), "DXF: file written");
    Check(data.find("$ACADVER") != std::string::npos &&
          data.find("AC1015") != std::string::npos, "DXF: R2000 header");
    Check(data.rfind("EOF") != std::string::npos, "DXF: ends with EOF");
    Check(data.find("\nArtwork\n") != std::string::npos, "DXF: layer name present");
    Check(data.find("HATCH") != std::string::npos, "DXF: fills as HATCH");
    Check(data.find("SPLINE") != std::string::npos, "DXF: curves as SPLINE");
    Check(data.find("UC_DASH1") != std::string::npos, "DXF: dash linetype defined");

    // ===== ezdxf (reference DXF implementation) =====
    if (HaveTool("python3 -c 'import ezdxf' >/dev/null 2>&1")) {
        std::string script = base + "_check.py";
        {
            std::ofstream f(script);
            f << "import sys, ezdxf\n"
                 "doc = ezdxf.readfile('" << dxfPath << "')\n"
                 "a = doc.audit()\n"
                 "assert not a.errors, a.errors\n"
                 "msp = doc.modelspace()\n"
                 "types = [e.dxftype() for e in msp]\n"
                 "assert types.count('HATCH') == 5, types\n"
                 "assert 'SPLINE' in types and 'LWPOLYLINE' in types, types\n"
                 "assert types.count('TEXT') == 1, types\n"
                 "assert 'Artwork' in [l.dxf.name for l in doc.layers], 'layer'\n"
                 "hatch = msp.query('HATCH')[0]\n"
                 "assert hatch.dxf.solid_fill == 1\n"
                 "assert hatch.dxf.true_color == 0xFF0000, hex(hatch.dxf.true_color)\n"
                 "spline = msp.query('SPLINE')[0]\n"
                 "assert spline.dxf.degree == 3\n"
                 "print('EZDXF_OK')\n";
        }
        int rc = std::system(("python3 " + script + " 2>&1 | tail -1 | "
                              "grep -q EZDXF_OK").c_str());
        Check(rc == 0, "DXF/ezdxf: strict read, clean audit, structure verified");
        if (rc != 0) std::system(("python3 " + script).c_str());   // show why
        std::remove(script.c_str());
    } else {
        std::printf("  note: ezdxf not installed; reference validation skipped\n");
    }

    // ===== LibreOffice rasterization =====
    if (HaveTool("command -v soffice >/dev/null 2>&1")) {
        std::string png = base + ".png";
        std::remove(png.c_str());
        std::system(("soffice --headless --convert-to png --outdir . " + dxfPath +
                     " >/dev/null 2>&1").c_str());
        if (std::ifstream(png).fail()) {
            std::printf("  note: LibreOffice produced no output; "
                        "render checks skipped\n");
        } else {
            Check(true, "DXF/soffice: external converter accepts the file");
            CheckRaster("DXF/soffice", png);
        }
    } else {
        std::printf("  note: LibreOffice not found; render checks skipped\n");
    }

    // ===== DWG via LibreDWG =====
    VectorConverter::DWGConverter dwg;
    if (!VectorConverter::DWGConverter::FindDxf2Dwg().empty()) {
        std::string dwgPath = base + ".dwg";
        Check(dwg.Export(*doc, dwgPath, options), "DWG: dxf2dwg conversion succeeds");
        Check(dwg.ValidateFile(dwgPath), "DWG: file carries the AC10xx magic");
        if (HaveTool("command -v dwgread >/dev/null 2>&1")) {
            Check(std::system(("dwgread " + dwgPath + " >/dev/null 2>&1").c_str()) == 0,
                  "DWG: dwgread accepts the file");
        }
    } else {
        std::string out = dwg.ExportToString(*doc, options);
        Check(out.empty(), "DWG: without dxf2dwg the export declines cleanly");
        std::printf("  note: LibreDWG dxf2dwg not found; DWG conversion skipped\n");
    }

    std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}
