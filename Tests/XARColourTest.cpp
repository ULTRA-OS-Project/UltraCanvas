// Tests/XARColourTest.cpp
// Locks down how the XAR plugin resolves TAG_DEFINECOMPLEXCOLOUR records to
// screen colours.
//
// This guards a regression that was invisible to every structural check: the
// files parsed, every node appeared, every path was in the right place - and
// the drawings still came out wrong, because roughly half of their colours
// resolved to near-black. Xara's Shade colours store two SIGNED parameters
// relative to a parent colour, not absolute colour coordinates, so recomputing
// the colour from them collapsed Apple5's soft gradients into hard dark bands
// and drained Midget's bodywork.
//
// The expectations below are the RGB triples the files themselves carry at the
// head of each colour record - Xara's own colour-managed screen values, which
// match the preview bitmap each file embeds. They cover the cases that broke:
// Shade colours over HSV and over CMYK parents, a Shade with negative
// parameters, and plain CMYK (whose screen appearance comes from Xara's
// separation tables and is not reproducible from a (1-ink)(1-K) formula).
//
// Usage: XARColourTest [samples-dir]
// Exit code is the number of failed expectations.
// Version: 1.0.0
// Last Modified: 2026-09-14
// Author: UltraCanvas Framework

#include "../UltraCanvas/Plugins/Vector/XAR/UltraCanvasXARPlugin.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace {

struct ColourExpectation {
    int32_t ref;                // colour record's sequence number in the file
    uint8_t r, g, b;
    const char* what;           // colour model / type, for the failure message
};

struct SampleExpectations {
    const char* file;
    std::vector<ColourExpectation> colours;
};

const std::vector<SampleExpectations>& Expectations() {
    static const std::vector<SampleExpectations> samples = {
        { "Apple5.xar", {
            {  112,  47, 161,  49, "HSV normal (shade parent)" },
            {  113,   6, 145,   8, "HSV shade of 112" },
            {  291,  33,  27,   3, "RGB normal" },
            { 3332,   9, 106,   6, "HSV shade, negative parameters" },
            { 3818, 255, 249, 176, "CMYK normal" },
            { 4591, 221, 255, 158, "HSV shade, brightest highlight" },
        }},
        { "Midget.xar", {
            {   62, 109, 110, 113, "CMYK normal (shade parent)" },
            {  519, 239, 238, 239, "CMYK shade of 62" },
            {  151, 225, 166, 172, "HSV normal (shade parent)" },
            { 1563, 234, 172, 178, "HSV shade of 151" },
            { 2319, 198, 138,  47, "CMYK shade of 62" },
        }},
    };
    return samples;
}

int CheckSample(const std::string& dir, const SampleExpectations& sample) {
    const std::string path = dir + "/" + sample.file;
    std::printf("%s\n", path.c_str());

    XARDocument doc;
    if (!doc.LoadFromFile(path)) {
        std::printf("  FAIL: could not load\n");
        return static_cast<int>(sample.colours.size());
    }

    int failures = 0;
    for (const auto& e : sample.colours) {
        const Color c = doc.ResolveColorRef(e.ref);
        const bool ok = c.r == e.r && c.g == e.g && c.b == e.b;
        if (!ok) ++failures;
        std::printf("  %s colour %5d  expected (%3u,%3u,%3u)  got (%3u,%3u,%3u)  %s\n",
                    ok ? "ok  " : "FAIL", e.ref,
                    e.r, e.g, e.b, c.r, c.g, c.b, e.what);
    }
    return failures;
}

}  // namespace

int main(int argc, char** argv) {
    std::string dir = (argc > 1) ? argv[1] : XAR_SAMPLES_DIR;

    int failures = 0;
    for (const auto& sample : Expectations()) {
        failures += CheckSample(dir, sample);
    }

    std::printf("\n%s (%d failed expectation(s))\n",
                failures == 0 ? "PASS" : "FAIL", failures);
    return failures;
}
