// Tests/SVGLocaleTest.cpp
// SVG numbers are dot-decimal by specification. This pins that the SVG reader
// and writer agree, whatever LC_NUMERIC the desktop is running under.
//
// The bug this guards: the Linux backend calls setlocale(LC_ALL, "") before
// opening the display, because XIM needs LC_CTYPE for UTF-8 input - and that
// also sets LC_NUMERIC. std::stof / strtod / atof then read "0.25" as 0 on a
// comma-decimal desktop (de_DE, fr_FR, ru_RU, pt_BR ...), so an SVG
// `opacity="0.25"` made the element fully transparent and `stroke-width="1.5"`
// drew at 1. The writer had the mirror defect: snprintf("%.6g") emitted
// `1,5`, and since a comma separates coordinates in path data, `M 1,5` was
// then re-read as "move to (1, 5)" - a different picture, not a broken file.
//
// Every check below runs twice: once under "C", once under whichever
// comma-decimal locale the machine has. The two runs must agree exactly.
// Where the machine has no such locale installed (a bare CI runner often does
// not) the second run is reported as skipped rather than silently passing -
// see the note at the bottom of main().
//
// Usage: SVGLocaleTest
// Exit code is the number of failed checks.
// Version: 1.0.0
// Last Modified: 2026-09-15
// Author: UltraCanvas Framework

#include "../UltraCanvas/Plugins/Vector/UltraCanvasVectorConverter.h"
#include "../UltraCanvas/Plugins/Vector/UltraCanvasVectorStorage.h"
#include "UltraCanvasTextUtils.h"

#include <clocale>
#include <cmath>
#include <cstdio>
#include <string>

using namespace UltraCanvas;
using namespace UltraCanvas::VectorStorage;

namespace {

int failures = 0;
const char* currentLocale = "C";

void Check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::printf("FAIL [%s]: %s\n", currentLocale, what.c_str());
    } else {
        std::printf("  ok [%s]: %s\n", currentLocale, what.c_str());
    }
}

bool Near(double a, double b, double tolerance = 0.001) {
    return std::fabs(a - b) < tolerance;
}

// The SVG the reader has to get right. Every number here is one the C locale
// reads correctly and a comma-decimal locale used to truncate at the '.'.
const char* kSvg = R"SVG(<?xml version="1.0"?>
<svg xmlns="http://www.w3.org/2000/svg" width=" +200.5 " height="100.5">
  <defs>
    <linearGradient id="g" x1="0" y1="0" x2="1" y2="0">
      <stop offset="0" stop-color="#ff0000"/>
      <stop offset="0.75" stop-color="#0000ff" stop-opacity="0.8"/>
    </linearGradient>
  </defs>
  <rect x="10.5" y="20.25" width="60.5" height="30.5"
        fill="url(#g)" opacity="0.25" stroke="#000000"
        stroke-width="1.5" stroke-miterlimit="2.5" stroke-dashoffset="3.5"/>
</svg>)SVG";

// ---------------------------------------------------------------------------

void CheckSharedParser() {
    float f = -1.0f;
    Check(TryParseFloat("0.25", f) && Near(f, 0.25), "TryParseFloat reads 0.25");
    Check(TryParseFloat("1.5", f) && Near(f, 1.5), "TryParseFloat reads 1.5");
    Check(TryParseFloat("-0.5", f) && Near(f, -0.5), "TryParseFloat reads -0.5");
    Check(TryParseFloat("+10", f) && Near(f, 10.0), "TryParseFloat accepts a leading '+' (SVG path data writes it)");
    Check(TryParseFloat("  2.5  ", f) && Near(f, 2.5), "TryParseFloat skips leading whitespace, as std::stof did");
    Check(TryParseFloat("10.5px", f) && Near(f, 10.5), "TryParseFloat ignores a trailing unit, as std::stof did");

    // A comma is never a decimal point in these formats, whatever the locale:
    // "1,5" is one number followed by a separator.
    Check(TryParseFloat("1,5", f) && Near(f, 1.0), "TryParseFloat stops at a comma (a separator, never a decimal point)");

    // Malformed input keeps the caller's default instead of throwing, which is
    // what std::stof did and what made an unguarded parse a crash risk.
    f = 7.5f;
    Check(!TryParseFloat("", f) && Near(f, 7.5), "TryParseFloat leaves the default on empty input");
    Check(!TryParseFloat("inherit", f) && Near(f, 7.5), "TryParseFloat leaves the default on a keyword");
    Check(!TryParseFloat("-", f) && Near(f, 7.5), "TryParseFloat leaves the default on a lone sign");

    // from_chars parity for the prefix scanner: "1.5em" ends at "1.5", because
    // that 'e' begins a unit and not an exponent.
    const std::string em = "1.5em";
    float scanned = 0.0f;
    const char* end = ParseFloatClassic(em.data(), em.data() + em.size(), scanned);
    Check(Near(scanned, 1.5) && end == em.data() + 3,
          "ParseFloatClassic stops before a unit that starts like an exponent");

    // The double overload keeps double precision rather than rounding through
    // a float on the way.
    double d = 0.0;
    Check(TryParseFloat("0.1234567890123", d) && Near(d, 0.1234567890123, 1e-12),
          "TryParseFloat's double overload keeps double precision");
}

void CheckImport() {
    VectorConverter::SVGConverter converter;
    VectorConverter::ConversionOptions options;

    auto doc = converter.ImportFromString(kSvg, options);
    Check(doc != nullptr, "SVG imports");
    if (!doc) return;

    // ParseLength's own path: strtod used to skip the padding whitespace and
    // the leading '+' for free, and the replacement has to keep doing so.
    Check(Near(doc->Size.width, 200.5), "width=\" +200.5 \" is 200.5 (padding and '+' survive)");
    Check(Near(doc->Size.height, 100.5), "height=\"100.5\" is 100.5");

    auto layer = doc->Layers.empty() ? nullptr : doc->Layers[0];
    auto rect = (layer && !layer->Children.empty())
            ? std::dynamic_pointer_cast<VectorRect>(layer->Children[0]) : nullptr;
    Check(rect != nullptr, "the rect imports");
    if (!rect) return;

    // The headline defect: opacity 0.25 read as 0 rendered the shape invisible.
    Check(Near(rect->Style.Opacity, 0.25), "opacity=\"0.25\" is 0.25, not 0");
    Check(rect->Style.Opacity > 0.0f, "opacity is not zero (the shape is still visible)");

    Check(Near(rect->Bounds.x, 10.5), "x=\"10.5\" is 10.5");
    Check(Near(rect->Bounds.y, 20.25), "y=\"20.25\" is 20.25");
    Check(Near(rect->Bounds.width, 60.5), "width=\"60.5\" is 60.5");

    Check(rect->Style.Stroke && Near(rect->Style.Stroke->Width, 1.5),
          "stroke-width=\"1.5\" is 1.5");
    Check(rect->Style.Stroke && Near(rect->Style.Stroke->MiterLimit, 2.5),
          "stroke-miterlimit=\"2.5\" is 2.5");
    Check(rect->Style.Stroke && Near(rect->Style.Stroke->DashOffset, 3.5),
          "stroke-dashoffset=\"3.5\" is 3.5");

    const GradientData* gd = rect->Style.Fill
            ? std::get_if<GradientData>(&*rect->Style.Fill) : nullptr;
    const LinearGradientData* lg = gd ? std::get_if<LinearGradientData>(gd) : nullptr;
    Check(lg && lg->Stops.size() == 2, "the gradient keeps both stops");
    if (lg && lg->Stops.size() == 2) {
        Check(Near(lg->Stops[1].position, 0.75), "stop offset=\"0.75\" is 0.75");
        // 0.8 * 255 = 204. Read as 0, the stop became fully transparent.
        Check(lg->Stops[1].color.a == 204, "stop-opacity=\"0.8\" is alpha 204, not 0");
    }
}

void CheckExportRoundTrip() {
    VectorConverter::SVGConverter converter;
    VectorConverter::ConversionOptions options;

    auto doc = std::make_shared<VectorDocument>();
    doc->Size = Size2Dd{200, 100};
    auto layer = doc->AddLayer("Artwork");

    auto rect = std::make_shared<VectorRect>();
    rect->Bounds = Rect2Dd{10.5, 20.25, 60.5, 30.5};
    rect->Style.Fill = Color(255, 0, 0, 255);
    rect->Style.Opacity = 0.25f;
    StrokeData stroke;
    stroke.Fill = Color(0, 0, 0, 255);
    stroke.Width = 1.5f;
    rect->Style.Stroke = stroke;
    layer->AddChild(rect);

    const std::string svg = converter.ExportToString(*doc, options);
    Check(!svg.empty(), "the document exports");
    if (svg.empty()) return;

    // Written with a '.', so any other SVG tool can read it back.
    Check(svg.find("1.5") != std::string::npos, "the writer emits \"1.5\"");
    Check(svg.find("1,5") == std::string::npos, "the writer never emits \"1,5\"");
    Check(svg.find("0.25") != std::string::npos, "the writer emits \"0.25\"");
    Check(svg.find("10.5") != std::string::npos, "the writer emits \"10.5\"");

    // And the whole way round, which is what a user actually does: save, reopen.
    auto back = converter.ImportFromString(svg, options);
    Check(back != nullptr, "the exported SVG imports back");
    if (!back) return;
    auto backLayer = back->Layers.empty() ? nullptr : back->Layers[0];
    auto backRect = (backLayer && !backLayer->Children.empty())
            ? std::dynamic_pointer_cast<VectorRect>(backLayer->Children[0]) : nullptr;
    Check(backRect != nullptr, "the rect survives the round trip");
    if (!backRect) return;
    Check(Near(backRect->Style.Opacity, 0.25), "opacity survives the round trip as 0.25");
    Check(backRect->Style.Stroke && Near(backRect->Style.Stroke->Width, 1.5),
          "stroke width survives the round trip as 1.5");
    Check(Near(backRect->Bounds.x, 10.5), "x survives the round trip as 10.5");
}

void RunAllChecks() {
    CheckSharedParser();
    CheckImport();
    CheckExportRoundTrip();
}

// Returns the name of a comma-decimal locale this machine can actually set, or
// nullptr. The candidates are the common desktop ones; which are installed is
// a property of the machine, not of the framework.
const char* FindCommaDecimalLocale() {
    static const char* candidates[] = {
        "de_DE.UTF-8", "de_DE.utf8", "de_DE",
        "fr_FR.UTF-8", "fr_FR.utf8", "fr_FR",
        "ru_RU.UTF-8", "ru_RU.utf8",
        "pt_BR.UTF-8", "pt_BR.utf8",
        "es_ES.UTF-8", "it_IT.UTF-8", "nl_NL.UTF-8",
        "German_Germany.1252",   // the MSVC/Windows spelling
        "French_France.1252",
    };
    for (const char* name : candidates) {
        if (std::setlocale(LC_ALL, name) != nullptr) {
            // Installed is not enough - confirm it really is comma-decimal,
            // so a locale that falls back to '.' does not pose as the test.
            const struct lconv* conv = std::localeconv();
            if (conv && conv->decimal_point && conv->decimal_point[0] == ',') {
                std::setlocale(LC_ALL, "C");
                return name;
            }
        }
    }
    std::setlocale(LC_ALL, "C");
    return nullptr;
}

} // namespace

int main() {
    std::printf("SVGLocaleTest: SVG numbers are dot-decimal whatever the locale\n\n");

    std::setlocale(LC_ALL, "C");
    currentLocale = "C";
    std::printf("--- LC_ALL=C ---\n");
    RunAllChecks();
    const int failuresUnderC = failures;

    const char* comma = FindCommaDecimalLocale();
    if (comma) {
        std::printf("\n--- LC_ALL=%s (comma-decimal) ---\n", comma);
        std::setlocale(LC_ALL, comma);
        currentLocale = comma;
        RunAllChecks();
        std::setlocale(LC_ALL, "C");

        const int failuresUnderComma = failures - failuresUnderC;
        currentLocale = "both";
        Check(failuresUnderComma == failuresUnderC,
              "the comma-decimal run agrees with the C run");
    } else {
        // Deliberately not a failure: no comma-decimal locale is installed
        // here, and a test cannot install one. The C run above still passed,
        // so this reports honestly instead of claiming coverage it did not get.
        std::printf("\n--- comma-decimal run SKIPPED ---\n");
        std::printf("    No comma-decimal locale is installed on this machine, so the\n"
                    "    half of this test that actually reproduces the bug did not run.\n"
                    "    To get the coverage, install one, e.g.\n"
                    "        sudo localedef -i de_DE -f UTF-8 de_DE.UTF-8\n");
    }

    std::printf("\n%s: %d check(s) failed\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures;
}
