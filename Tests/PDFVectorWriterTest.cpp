// Tests/PDFVectorWriterTest.cpp
// Test for the Vector plugin's PDF writer: builds the same document the
// other writer tests use, exports it, and validates the PDF on three
// levels: structure (header, object layout, an xref table whose offsets
// actually point at the objects they claim), content (the expected
// operators and resources are present), and rendering - when ghostscript is
// available the export is rasterized and pixel-checked through the image
// pipeline, proving an independent consumer accepts the file.
//
// Usage: PDFVectorWriterTest [output.pdf]
// Exit code is the number of failed checks.
// Version: 1.0.0
// Last Modified: 2026-08-26
// Author: UltraCanvas Framework

#include "../UltraCanvas/Plugins/Vector/UltraCanvasVectorConverter.h"
#include "../UltraCanvas/Plugins/Vector/UltraCanvasVectorStorage.h"
#include "UltraCanvasImage.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
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
    doc->Title = "PDF writer test";

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
    circle->Style.Opacity = 0.5f;
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
    text->BaseStyle.FontFamily = "Helvetica";
    text->BaseStyle.FontSize = 18.0f;
    TextSpanData s1;
    s1.Text = "Hello ";
    s1.Style = text->BaseStyle;
    TextSpanData s2;
    s2.Text = "PDF (vector)";
    s2.Style = text->BaseStyle;
    s2.Style.Weight = FontWeight::Bold;
    text->Spans.push_back(s1);
    text->Spans.push_back(s2);
    text->Style.Fill = Color(20, 20, 20, 255);
    layer->AddChild(text);

    return doc;
}

// The xref table is only honest if each entry's offset points at the very
// object it indexes.
bool XrefSelfConsistent(const std::string& pdf) {
    size_t xref = pdf.rfind("\nxref\n");   // not the tail of "startxref"
    if (xref == std::string::npos) return false;
    size_t pos = xref + 6;
    size_t nl = pdf.find('\n', pos);
    int count = 0;
    if (std::sscanf(pdf.substr(pos, nl - pos).c_str(), "0 %d", &count) != 1) return false;
    pos = nl + 1;
    pos = pdf.find('\n', pos) + 1;   // skip the free-list entry
    for (int i = 1; i < count; ++i) {
        unsigned long off = 0;
        if (std::sscanf(pdf.c_str() + pos, "%lu", &off) != 1) return false;
        char expect[32];
        std::snprintf(expect, sizeof(expect), "%d 0 obj", i);
        if (pdf.compare(off, std::strlen(expect), expect) != 0) return false;
        pos = pdf.find('\n', pos) + 1;
    }
    return true;
}

}   // namespace

int main(int argc, char** argv) {
    std::string outPath = argc > 1 ? argv[1] : "pdf_writer_test.pdf";

    auto doc = BuildTestDocument();
    VectorConverter::PDFVectorConverter converter;
    VectorConverter::ConversionOptions options;
    options.WarningCallback = [](const std::string& msg) {
        std::printf("      warning: %s\n", msg.c_str());
    };

    std::string pdf = converter.ExportToString(*doc, options);
    Check(!pdf.empty(), "ExportToString produces output");
    Check(converter.ValidateData(pdf), "output starts with %PDF-");
    Check(converter.Export(*doc, outPath, options), "Export() writes the file");
    Check(pdf.rfind("%%EOF") != std::string::npos, "trailer ends with %%EOF");
    Check(XrefSelfConsistent(pdf), "xref offsets point at their objects");
    Check(pdf.find("/MediaBox [0 0 400 300]") != std::string::npos,
          "MediaBox matches the page size");
    Check(pdf.find("/BaseFont /Helvetica-Bold") != std::string::npos,
          "the bold span registers Helvetica-Bold");
    Check(pdf.find("/ExtGState") != std::string::npos &&
          pdf.find("/ca 0.5") != std::string::npos,
          "50% opacity becomes an ExtGState");
    Check(pdf.find("[6 3] 0 d") != std::string::npos, "dash pattern serialized");
    Check(pdf.find("Hello ") != std::string::npos &&
          pdf.find("PDF \\(vector\\)") != std::string::npos,
          "text with escaped parentheses lands in the content stream");

    // ===== INDEPENDENT CONSUMER (ghostscript, when present) =====
    if (std::system("command -v gs >/dev/null 2>&1") == 0) {
        std::string png = outPath + ".png";
        std::string cmd = "gs -q -dSAFER -dBATCH -dNOPAUSE -sDEVICE=png16m -r72 "
                          "-sOutputFile=" + png + " " + outPath + " >/dev/null 2>&1";
        Check(std::system(cmd.c_str()) == 0, "ghostscript accepts the file");

        UCImage::InitializeImageSubsysterm("PDFVectorWriterTest");
        auto img = UCImage::Get(png);
        Check(img && img->GetWidth() == 400 && img->GetHeight() == 300,
              "rendered page is 400x300 at 72dpi");
        if (img) {
            auto pm = img->GetPixmap(400, 300, ImageFitMode::Contain, 1.0f);
            Check(pm != nullptr, "rendered page decodes");
            if (pm) {
                const uint32_t* px = pm->GetPixelData();
                int w = pm->GetRawWidth();
                auto at = [&](int x, int y) { return px[y * w + x]; };
                uint32_t rectPx = at(90, 70);
                Check(((rectPx >> 16) & 0xFF) > 200 && ((rectPx >> 8) & 0xFF) < 60,
                      "rect renders red at (90,70)");
                uint32_t circlePx = at(330, 70);
                Check(((circlePx >> 16) & 0xFF) > 200 && ((circlePx >> 8) & 0xFF) > 150,
                      "50% orange circle renders pale at (330,70)");
                uint32_t rotPx = at(330, 180);
                Check((rotPx & 0xFF) > 120 && ((rotPx >> 16) & 0xFF) < 100,
                      "rotated rect renders blue at (330,180)");
                uint32_t bgPx = at(20, 280);
                Check((bgPx & 0xFFFFFF) == 0xFFFFFF, "background stays white");
            }
        }
    } else {
        std::printf("  note: ghostscript not found; render checks skipped\n");
    }

    std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}
