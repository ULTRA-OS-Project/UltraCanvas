// Tests/VectorLocaleTest.cpp
// Every vector writer produces the same file whatever locale the host runs in.
//
// The model writers had this defect in five places (0.9.532): a stream renders
// its numbers through the C++ global locale, which a host application sets
// when it honours the desktop's language, so a writer that streams - or
// snprintf's - its numbers wrote the desktop into the file. In a drawing
// format that is not a rounding error but a different drawing: `M 1,5` in SVG
// path data reads back as the point (1, 5), a PDF content stream's `1,5 0 0
// 1,5 0 0 cm` is a syntax error in an operator that takes six numbers, and a
// grouped `1.000` where a count or an object id was meant breaks the file for
// every reader.
//
// scripts/check_locale_numbers.py catches a number built as a string, and it
// is what emptied this corner of its baseline - but it reads one file at a
// time, cannot see the locale of a stream a writer was handed, and skips a
// whole file that mentions the idiom anywhere, which is how the SVG reader
// kept three unimbued streams beside two imbued ones. So this test is what
// holds the vector formats to it, in the shape ModelLocaleDecimalTest uses:
// one drawing, written by every writer twice, and the two runs must agree
// byte for byte. None of these writers embeds a date, so anything that
// differs is the locale. Where the format can be read back, the file is also
// read under both locales and the two drawings compared.
//
// Usage: VectorLocaleTest
// Exit code is the number of failed checks.
// Version: 1.0.0
// Last Modified: 2026-11-12
// Author: UltraCanvas Framework

#include "LocaleTestSupport.h"   // InLocale, CommaDecimalLocale - both switches

#include "UltraCanvasVectorConverter.h"
#include "UltraCanvasCADConverters.h"
#include "UltraCanvasCDRConverter.h"
#include "UltraCanvasEPSConverter.h"
#include "UltraCanvasMetafileConverters.h"
#include "UltraCanvasXARConverter.h"
#include "DataFormats/UltraCanvasVectorStorage.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;
using namespace UltraCanvas::VectorStorage;
using namespace UltraCanvas::VectorConverter;
using UltraCanvasTest::CommaDecimalLocale;
using UltraCanvasTest::InLocale;

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++failures;
}

// A drawing whose every number says something about the locale: a fractional
// size, fractional coordinates, a dash pattern, a gradient stop, a rotation,
// a text size - and a coordinate past a thousand, because digit grouping is
// the half that is easy to miss and it needs four digits to show. The 1200
// lines are there for the same reason: they push object counts, handles and
// ids past 1000 in the formats that number what they write.
std::shared_ptr<VectorDocument> BuildDocument() {
    auto document = std::make_shared<VectorDocument>();
    document->Size = Size2Dd{420.5, 297.25};
    document->Title = "VectorLocaleTest";

    auto layer = document->AddLayer("Artwork");

    auto rect = std::make_shared<VectorRect>();
    rect->Bounds = Rect2Dd{10.5, 20.25, 123456.75, 60.5};
    rect->RadiusX = 12.5;
    rect->RadiusY = 12.5;
    rect->Style.Fill = Color(255, 0, 0, 255);
    rect->Style.Opacity = 0.25f;
    StrokeData stroke;
    stroke.Fill = Color(0, 0, 255, 255);
    stroke.Width = 1.5f;
    stroke.MiterLimit = 2.5f;
    stroke.DashArray = {6.5, 3.25};
    stroke.DashOffset = 1.75f;
    rect->Style.Stroke = stroke;
    layer->AddChild(rect);

    auto circle = std::make_shared<VectorCircle>();
    circle->Center = Point2Dd(330.5, 70.25);
    circle->Radius = 30.75;
    circle->Style.Fill = Color(255, 128, 0, 255);
    layer->AddChild(circle);

    auto path = std::make_shared<VectorPath>();
    path->MoveTo(200.5, 140.25);
    path->CurveTo(240.5, 120.25, 280.75, 120.5, 300.25, 160.75);
    path->ClosePath();
    path->Style.Fill = Color(90, 40, 160, 255);
    layer->AddChild(path);

    auto group = std::make_shared<VectorGroup>();
    group->Transform = Matrix3x3::Translate(330.5, 180.25) *
                       Matrix3x3::RotateDegrees(30.5) *
                       Matrix3x3::Translate(-330.5, -180.25);
    auto rotated = std::make_shared<VectorRect>();
    rotated->Bounds = Rect2Dd{300.5, 160.25, 60.75, 40.5};
    rotated->Style.Fill = Color(0, 120, 200, 255);
    group->AddChild(rotated);
    layer->AddChild(group);

    auto text = std::make_shared<VectorText>();
    text->Position = Point2Dd(40.5, 250.25);
    text->BaseStyle.FontFamily = "Liberation Sans";
    text->BaseStyle.FontSize = 18.5f;
    text->SetText("Hello locale");
    text->Style.Fill = Color(20, 20, 20, 255);
    layer->AddChild(text);

    auto many = document->AddLayer("Thousands");
    for (int i = 0; i < 1200; ++i) {
        auto line = std::make_shared<VectorLine>();
        line->Start = Point2Dd(0.5 + i * 0.25, 1.5);
        line->End = Point2Dd(1.5 + i * 0.25, 2.5);
        StrokeData thin;
        thin.Fill = Color(0, 0, 0, 255);
        thin.Width = 0.25f;
        line->Style.Stroke = thin;
        many->AddChild(line);
    }
    return document;
}

template <class T>
std::unique_ptr<IVectorFormatConverter> Make() { return std::make_unique<T>(); }

using Factory = std::unique_ptr<IVectorFormatConverter> (*)();

// Every writer the Vector plugin carries. A new one belongs here: nothing
// enumerates them, because the facade that could (CreateConverterForExtension)
// reaches the UI stack and this test deliberately does not.
struct WriterCase {
    const char* name;
    Factory make;
};

// Where the two runs differ, the byte they part at and what sits around it -
// the number that carries the locale is always right there.
std::string Difference(const std::string& inC, const std::string& inComma) {
    size_t at = 0;
    while (at < inC.size() && at < inComma.size() && inC[at] == inComma[at]) ++at;
    const size_t from = at > 40 ? at - 40 : 0;
    const auto piece = [&](const std::string& text) {
        return text.substr(from, std::min(text.size(), at + 20) - from);
    };
    return " - they part at byte " + std::to_string(at) + ": \"" + piece(inC) +
           "\" against \"" + piece(inComma) + "\"";
}

} // namespace

int main() {
    const char* comma = CommaDecimalLocale();
    if (!comma) {
        std::printf("No comma-decimal locale installed: comparison skipped.\n");
        return 0;
    }
    std::printf("Comparing \"C\" with \"%s\"\n", comma);

    const std::shared_ptr<VectorDocument> document = BuildDocument();
    const std::vector<WriterCase> writers = {
        {"SVG", &Make<SVGConverter>},
        {"PDF", &Make<PDFVectorConverter>},
        {"EPS", &Make<EPSConverter>},
        {"DXF", &Make<DXFConverter>},
        {"DWG", &Make<DWGConverter>},
        {"EMF", &Make<EMFConverter>},
        {"WMF", &Make<WMFConverter>},
        {"AI", &Make<AIConverter>},
        {"CDR", &Make<CDRConverter>},
        {"XAR", &Make<XARConverter>},
    };

    for (const WriterCase& test : writers) {
        std::unique_ptr<IVectorFormatConverter> writer = test.make();
        if (!writer->CanExport()) {
            std::printf("  [SKIP] %s (no writer in this build)\n", test.name);
            continue;
        }
        // A writer that declines on this machine says why through the warning
        // callback - DWG hands the drawing to LibreDWG's dxf2dwg and writes
        // nothing when that tool is absent - so a skip can name the reason
        // rather than read as a pass.
        std::string declined;
        ConversionOptions options;
        options.WarningCallback = [&declined](const std::string& message) {
            if (declined.empty()) declined = message;
        };
        const std::string inC = writer->ExportToString(*document, options);
        if (inC.empty()) {
            std::printf("  [SKIP] %s (writes nothing here: %s)\n", test.name,
                        declined.empty() ? "no reason given" : declined.c_str());
            continue;
        }

        std::string inComma;
        {
            const InLocale german(comma);
            if (!german.HasGlobal()) {
                // Without the C++ global locale every stream writes '.'
                // whatever the writer does, so the comparison would pass on a
                // writer that has the defect.
                std::printf("  [SKIP] %s (\"%s\" is not a C++ locale here)\n",
                            test.name, comma);
                continue;
            }
            inComma = writer->ExportToString(*document, options);
        }

        Check(!inComma.empty(), std::string(test.name) + " writes the document in \"" +
                                       comma + "\"");
        const bool same = inC == inComma;
        Check(!inComma.empty() && same,
              std::string(test.name) + ": the same " + std::to_string(inC.size()) +
                      " bytes in both" + (same ? "" : Difference(inC, inComma)));

        // Where the same format can be read back, the reader is held to it as
        // well: the file it is given is dot-decimal either way, so a drawing
        // read under the host's locale must come back identical. Comparing
        // what SVG makes of the two documents is the cheap way to compare the
        // documents themselves - and SVG is already held to this above.
        if (!writer->CanImport()) continue;
        SVGConverter svg;
        ConversionOptions quiet;
        auto readC = writer->ImportFromString(inC, quiet);
        std::shared_ptr<VectorDocument> readComma;
        {
            const InLocale german(comma);
            readComma = writer->ImportFromString(inC, quiet);
        }
        if (!readC || !readComma) {
            Check(static_cast<bool>(readC) == static_cast<bool>(readComma),
                  std::string(test.name) + ": read back the same way in both locales");
            continue;
        }
        const std::string asSvgC = svg.ExportToString(*readC, quiet);
        const std::string asSvgComma = svg.ExportToString(*readComma, quiet);
        const bool sameRead = asSvgC == asSvgComma;
        Check(sameRead, std::string(test.name) + ": read back as the same drawing in both" +
                                (sameRead ? "" : Difference(asSvgC, asSvgComma)));
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
